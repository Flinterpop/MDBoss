#include "Config.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "FileScan.h"
#include "PathUtf8.h"

namespace mdboss {
namespace {

using json = nlohmann::json;

// The caps now live in Config.h, shared with the favorites import path.

// getenv() is deprecated under /W4 /WX on MSVC, and the _s variant hands back
// an allocation the caller owns.
std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string out(value);
    std::free(value);
    return out;
}

std::string user_data_base()
{
    const std::string appdata = environment("APPDATA");
    if (!appdata.empty()) {
        return appdata;
    }
    const std::string profile = environment("USERPROFILE");
    return profile.empty() ? std::string(".") : profile;
}

// Read the whole config file as JSON, or a null json on any failure.
json read_document()
{
    std::ifstream stream(Config::path(), std::ios::binary);
    if (!stream) {
        return json{};
    }
    // Read through a buffer rather than parsing the stream directly, so a
    // BOM can be removed first: a JSON parser rejects one outright, and the
    // whole config would silently fall back to defaults -- losing the user's
    // roots and favorites -- if anything ever wrote the file with one.
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    json document =
        json::parse(strip_utf8_bom(buffer.str()), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return json{};
    }
    return document;
}

std::vector<std::string> string_array(const json& document, const char* key,
                                      std::size_t limit)
{
    std::vector<std::string> out;
    if (!document.contains(key) || !document[key].is_array()) {
        return out;
    }
    for (const json& item : document[key]) {
        if (out.size() >= limit) {
            break;
        }
        if (item.is_string()) {
            out.push_back(item.get<std::string>());
        }
    }
    return out;
}

}  // namespace

std::string user_data_dir()
{
    return path_to_utf8(path_from_utf8(user_data_base()) / "MDBoss");
}

std::string Config::path()
{
    return path_to_utf8(path_from_utf8(user_data_dir()) / "config.json");
}

void Config::load()
{
    const json document = read_document();
    if (!document.is_object()) {
        return;
    }

    roots_.clear();
    if (document.contains("roots") && document["roots"].is_array()) {
        for (const json& entry : document["roots"]) {
            if (!entry.is_object() || !entry.contains("path") ||
                !entry["path"].is_string()) {
                continue;
            }
            Root root;
            root.path = entry["path"].get<std::string>();
            root.name = (entry.contains("name") && entry["name"].is_string())
                            ? entry["name"].get<std::string>()
                            : root.path;
            roots_.push_back(std::move(root));
        }
    }

    favorites_ = string_array(document, "favorites", kMaxFavorites);
    recents_ = string_array(document, "recents", kMaxRecents);

    if (document.contains("hide_front_matter") &&
        document["hide_front_matter"].is_boolean()) {
        hide_front_matter_ = document["hide_front_matter"].get<bool>();
    }
    if (document.contains("wx_window_width") &&
        document["wx_window_width"].is_number_integer()) {
        window_width_ = document["wx_window_width"].get<int>();
    }
    if (document.contains("wx_window_height") &&
        document["wx_window_height"].is_number_integer()) {
        window_height_ = document["wx_window_height"].get<int>();
    }
    if (document.contains("wx_editor_sash") &&
        document["wx_editor_sash"].is_number_integer()) {
        editor_sash_ = document["wx_editor_sash"].get<int>();
    }
    if (document.contains("wx_files_sash") &&
        document["wx_files_sash"].is_number_integer()) {
        files_sash_ = document["wx_files_sash"].get<int>();
    }
    if (document.contains("wx_outline_sash") &&
        document["wx_outline_sash"].is_number_integer()) {
        outline_sash_ = document["wx_outline_sash"].get<int>();
    }
    for (const auto& [key, target] :
         {std::pair<const char*, bool*>{"wx_show_files", &show_files_},
          {"wx_show_outline", &show_outline_},
          {"wx_show_editor", &show_editor_}}) {
        if (document.contains(key) && document[key].is_boolean()) {
            *target = document[key].get<bool>();
        }
    }
    if (document.contains("wx_recent_sash") &&
        document["wx_recent_sash"].is_number_integer()) {
        recent_sash_ = document["wx_recent_sash"].get<int>();
    }
    if (document.contains("wx_favorites_sash") &&
        document["wx_favorites_sash"].is_number_integer()) {
        favorites_sash_ = document["wx_favorites_sash"].get<int>();
    }
}

bool Config::save() const
{
    // Re-read first so keys written by the Python app since we loaded --
    // including its opaque Qt geometry blobs -- survive.
    json document = read_document();
    if (!document.is_object()) {
        document = json::object();
    }

    json roots = json::array();
    for (const Root& root : roots_) {
        roots.push_back(json{{"name", root.name}, {"path", root.path}});
    }
    document["roots"] = roots;
    document["favorites"] = favorites_;
    document["recents"] = recents_;
    document["hide_front_matter"] = hide_front_matter_;
    document["wx_window_width"] = window_width_;
    document["wx_window_height"] = window_height_;
    document["wx_editor_sash"] = editor_sash_;
    document["wx_files_sash"] = files_sash_;
    document["wx_outline_sash"] = outline_sash_;
    document["wx_show_files"] = show_files_;
    document["wx_show_outline"] = show_outline_;
    document["wx_show_editor"] = show_editor_;
    document["wx_recent_sash"] = recent_sash_;
    document["wx_favorites_sash"] = favorites_sash_;

    const std::filesystem::path file(path());
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream << document.dump(2) << '\n';
    return stream.good();
}

void Config::push_recent(const std::string& path)
{
    assert(!path.empty() && "a recent entry needs a path");
    for (auto it = recents_.begin(); it != recents_.end(); ++it) {
        if (*it == path) {
            recents_.erase(it);
            break;
        }
    }
    recents_.insert(recents_.begin(), path);
    if (recents_.size() > kMaxRecents) {
        recents_.resize(kMaxRecents);
    }
}

bool Config::is_favorite(const std::string& path) const
{
    for (const std::string& favorite : favorites_) {
        if (favorite == path) {
            return true;
        }
    }
    return false;
}

void Config::add_favorite(const std::string& path)
{
    assert(!path.empty() && "a favorite needs a path");
    remove_favorite(path);
    favorites_.insert(favorites_.begin(), path);
    if (favorites_.size() > kMaxFavorites) {
        favorites_.resize(kMaxFavorites);
    }
}

void Config::remove_favorite(const std::string& path)
{
    for (auto it = favorites_.begin(); it != favorites_.end(); ++it) {
        if (*it == path) {
            favorites_.erase(it);
            return;
        }
    }
}

void Config::set_window_size(int width, int height)
{
    // Ignore the degenerate sizes a minimised window reports.
    if (width < 200 || height < 200) {
        return;
    }
    window_width_ = width;
    window_height_ = height;
}

}  // namespace mdboss
