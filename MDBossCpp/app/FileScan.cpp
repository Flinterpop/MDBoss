#include "FileScan.h"

#include "PathUtf8.h"

#include <algorithm>
#include <cassert>
#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>

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
    // Matches app.py's MARKDOWN_EXTS; keep the two in step.
    const std::string ext = to_lower(name.substr(dot));
    return ext == ".md" || ext == ".markdown" || ext == ".mdown" ||
           ext == ".mkd" || ext == ".mdwn";
}

std::string norm_path(const std::string& path)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(path_from_utf8(path), ec);
    if (ec) {
        absolute = path_from_utf8(path);
    }
    return to_lower(path_to_utf8(absolute.lexically_normal()));
}

std::map<std::string, int> md_counts_for_root(const std::string& root)
{
    std::map<std::string, int> counts;
    std::error_code ec;
    if (!fs::is_directory(path_from_utf8(root), ec) || ec) {
        return counts;
    }

    // ONE walk.  An earlier version collected the directories and then
    // re-listed each of them, walking the tree twice and normalising every
    // path again on the second pass; over a few thousand files that was slow
    // enough to matter, and it ran on the UI thread.
    const fs::path root_path = path_from_utf8(root).lexically_normal();
    std::vector<fs::path> dirs{root_path};
    counts[norm_path(path_to_utf8(root_path))] = 0;

    // Iterate with the error_code-taking increment.  A range-for uses the
    // throwing operator++, which the error_code constructor does NOT make
    // safe: one unreadable directory mid-walk raises filesystem_error, and on
    // a worker thread that is an uncaught exception and a hard crash.
    fs::recursive_directory_iterator it(
        root_path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    int walked = 0;
    while (!ec && it != end && walked < kMaxWalkedDirs) {
        ++walked;
        const fs::directory_entry entry = *it;
        std::error_code kind_ec;
        if (entry.is_directory(kind_ec) && !kind_ec) {
            dirs.push_back(entry.path());
            counts.emplace(norm_path(path_to_utf8(entry.path())), 0);
        } else if (is_markdown(path_to_utf8(entry.path().filename()))) {
            const auto found =
                counts.find(norm_path(path_to_utf8(entry.path().parent_path())));
            if (found != counts.end()) {
                ++found->second;
            }
        }
        it.increment(ec);
    }

    // Roll the direct counts up, deepest first, so each parent adds its
    // children's finished totals exactly once.
    std::sort(dirs.begin(), dirs.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.native().size() > b.native().size();
              });
    for (const fs::path& dir : dirs) {
        if (dir == root_path) {
            continue;
        }
        const auto self = counts.find(norm_path(path_to_utf8(dir)));
        const auto parent =
            counts.find(norm_path(path_to_utf8(dir.parent_path())));
        if (self != counts.end() && parent != counts.end()) {
            parent->second += self->second;
        }
    }
    return counts;
}

bool send_to_recycle_bin(const std::string& path)
{
    assert(!path.empty() && "deleting an empty path would be a bug");
    if (path.empty()) {
        return false;
    }
    // SHFileOperation wants an absolute path and a DOUBLE-null-terminated
    // list, not a plain string.  A relative path silently resolves against
    // the process's current directory, which is not where the tree is.
    std::error_code ec;
    const fs::path absolute = fs::absolute(path_from_utf8(path), ec);
    if (ec) {
        return false;
    }
    std::wstring wide = absolute.wstring();
    wide.push_back(L'\0');
    wide.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = wide.c_str();
    // FOF_ALLOWUNDO is what routes this to the Recycle Bin rather than
    // deleting outright; without it the operation is unrecoverable.  The
    // caller has already confirmed, hence NOCONFIRMATION.
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                FOF_SILENT;
    return SHFileOperationW(&op) == 0 && op.fAnyOperationsAborted == FALSE;
}

std::string choose_dropped_file(const std::vector<std::string>& filenames)
{
    if (filenames.empty()) {
        return {};
    }
    for (const std::string& name : filenames) {
        // Match on the leaf: a directory called "notes.md" further up the
        // path must not make a .txt file look like Markdown.
        const std::size_t slash = name.find_last_of("/\\");
        const std::string leaf =
            (slash == std::string::npos) ? name : name.substr(slash + 1);
        if (is_markdown(leaf)) {
            return name;
        }
    }
    return filenames.front();
}

std::vector<Entry> list_directory(const std::string& path)
{
    std::vector<Entry> out;
    std::error_code ec;
    fs::directory_iterator it(
        path_from_utf8(path), fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    // Same reason as md_counts_for_root: increment(ec) rather than a
    // range-for, so an unreadable entry cannot throw.
    while (!ec && it != end && out.size() < kMaxEntriesPerDir) {
        const fs::directory_entry entry = *it;
        std::error_code kind_ec;
        const bool dir = entry.is_directory(kind_ec) && !kind_ec;
        const std::string name = path_to_utf8(entry.path().filename());
        if (dir || is_markdown(name)) {
            out.push_back(Entry{path_to_utf8(entry.path()), name, dir});
        }
        it.increment(ec);
    }

    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir;   // directories first
        }
        return to_lower(a.name) < to_lower(b.name);
    });
    return out;
}

bool operator==(const FileStamp& a, const FileStamp& b)
{
    // Two absent files compare equal whatever the other fields hold, so a
    // file that stays deleted is not reported as changing over and over.
    if (!a.exists || !b.exists) {
        return a.exists == b.exists;
    }
    return a.mtime_ticks == b.mtime_ticks && a.size == b.size;
}

bool operator!=(const FileStamp& a, const FileStamp& b)
{
    return !(a == b);
}

FileStamp stamp_of(const std::string& path)
{
    assert(!path.empty() && "stamp_of needs a path");
    FileStamp out;
    if (path.empty()) {
        return out;
    }

    const fs::path target = path_from_utf8(path);
    std::error_code ec;
    // Every one of these throws on failure without the error_code overload,
    // and a document can vanish between two of them, so each is checked
    // rather than relying on the status of the one before.
    if (!fs::is_regular_file(target, ec) || ec) {
        return out;
    }
    const std::uintmax_t size = fs::file_size(target, ec);
    if (ec) {
        return out;
    }
    const fs::file_time_type written = fs::last_write_time(target, ec);
    if (ec) {
        return out;
    }

    out.exists = true;
    out.size = static_cast<unsigned long long>(size);
    out.mtime_ticks = static_cast<long long>(written.time_since_epoch().count());
    return out;
}

}  // namespace mdboss
