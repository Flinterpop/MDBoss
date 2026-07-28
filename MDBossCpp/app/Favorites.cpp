#include "Favorites.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <set>

#include "FileScan.h"

namespace mdboss {
namespace {

using json = nlohmann::json;

// Bounded (Rule of 10): a hostile or corrupt file must not be walked forever.
constexpr std::size_t kMaxEntries = 10000;

bool is_blank(const std::string& text)
{
    for (const char c : text) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string favorites_to_json(const std::vector<std::string>& favorites)
{
    json document;
    // Always an array, even when empty: json's default for an unassigned key
    // would be null, which Python's reader rejects as "not a list".
    document["favorites"] = json::array();
    for (const std::string& path : favorites) {
        document["favorites"].push_back(path);
    }
    return document.dump(2);
}

FavoritesFile parse_favorites_json(const std::string& text)
{
    FavoritesFile out;
    // Non-throwing parse: a file the user picked by mistake is an ordinary
    // outcome here, not an exceptional one.
    const json document = json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        return out;
    }

    // Python reads `data.get("favorites") if isinstance(data, dict) else data`
    // -- an object carries the list under the key, and a bare array is taken
    // as the list itself.
    const json* array = nullptr;
    if (document.is_object()) {
        const auto found = document.find("favorites");
        if (found != document.end()) {
            array = &(*found);
        }
    } else if (document.is_array()) {
        array = &document;
    }
    if (array == nullptr || !array->is_array()) {
        return out;
    }

    out.parsed = true;
    std::size_t seen = 0;
    for (const json& entry : *array) {
        if (++seen > kMaxEntries) {
            break;
        }
        if (!entry.is_string()) {
            continue;
        }
        const std::string path = entry.get<std::string>();
        if (is_blank(path)) {
            continue;
        }
        out.paths.push_back(path);
    }
    return out;
}

std::vector<std::string> merge_favorites(
    const std::vector<std::string>& existing,
    const std::vector<std::string>& imported, bool merge, std::size_t cap)
{
    assert(cap > 0 && "a cap of zero would discard everything");

    std::vector<std::string> combined;
    if (merge) {
        combined = existing;
    }
    combined.insert(combined.end(), imported.begin(), imported.end());

    // First appearance wins, so merging keeps the user's own ordering and the
    // imported entries land after it.
    std::vector<std::string> deduped;
    std::set<std::string> keys;
    for (const std::string& path : combined) {
        if (deduped.size() >= cap) {
            break;
        }
        const std::string key = norm_path(path);
        if (keys.insert(key).second) {
            deduped.push_back(path);
        }
    }
    return deduped;
}

}  // namespace mdboss
