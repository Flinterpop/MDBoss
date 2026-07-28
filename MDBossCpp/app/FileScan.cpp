#include "FileScan.h"

#include <algorithm>
#include <cassert>
#include <filesystem>

namespace mdboss {
namespace {

namespace fs = std::filesystem;

// Bounded (Rule of 10): a pathological tree must not spin the UI.
constexpr int kMaxWalkedDirs = 100000;
constexpr std::size_t kMaxEntriesPerDir = 20000;

std::string to_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    });
    return text;
}

}  // namespace

bool is_markdown(const std::string& name)
{
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string ext = to_lower(name.substr(dot));
    return ext == ".md" || ext == ".markdown" || ext == ".mdown" ||
           ext == ".mkd";
}

std::string norm_path(const std::string& path)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(fs::path(path), ec);
    if (ec) {
        absolute = fs::path(path);
    }
    return to_lower(absolute.lexically_normal().string());
}

std::map<std::string, int> md_counts_for_root(const std::string& root)
{
    std::map<std::string, int> counts;
    std::error_code ec;
    if (!fs::is_directory(fs::path(root), ec) || ec) {
        return counts;
    }

    // Collect directories first, then total them deepest-first so each parent
    // can simply add its children's finished totals.
    std::vector<fs::path> dirs{fs::path(root)};
    fs::recursive_directory_iterator it(
        fs::path(root), fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        int walked = 0;
        for (const fs::directory_entry& entry : it) {
            if (++walked > kMaxWalkedDirs) {
                break;
            }
            std::error_code dir_ec;
            if (entry.is_directory(dir_ec) && !dir_ec) {
                dirs.push_back(entry.path());
            }
        }
    }

    std::sort(dirs.begin(), dirs.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.string().size() > b.string().size();
              });

    for (const fs::path& dir : dirs) {
        int total = 0;
        std::error_code list_ec;
        fs::directory_iterator entries(
            dir, fs::directory_options::skip_permission_denied, list_ec);
        if (list_ec) {
            continue;   // unreadable: leave it absent, not zero
        }
        std::size_t seen = 0;
        for (const fs::directory_entry& entry : entries) {
            if (++seen > kMaxEntriesPerDir) {
                break;
            }
            std::error_code entry_ec;
            if (entry.is_directory(entry_ec) && !entry_ec) {
                const auto found = counts.find(norm_path(entry.path().string()));
                if (found != counts.end()) {
                    total += found->second;
                }
            } else if (is_markdown(entry.path().filename().string())) {
                ++total;
            }
        }
        counts[norm_path(dir.string())] = total;
    }
    return counts;
}

std::vector<Entry> list_directory(const std::string& path)
{
    std::vector<Entry> out;
    std::error_code ec;
    fs::directory_iterator entries(
        fs::path(path), fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return out;
    }
    for (const fs::directory_entry& entry : entries) {
        if (out.size() >= kMaxEntriesPerDir) {
            break;
        }
        std::error_code kind_ec;
        const bool dir = entry.is_directory(kind_ec) && !kind_ec;
        const std::string name = entry.path().filename().string();
        if (!dir && !is_markdown(name)) {
            continue;
        }
        out.push_back(Entry{entry.path().string(), name, dir});
    }

    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir;   // directories first
        }
        return to_lower(a.name) < to_lower(b.name);
    });
    return out;
}

}  // namespace mdboss
