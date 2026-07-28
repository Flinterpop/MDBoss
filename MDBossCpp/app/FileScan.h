// Filesystem scanning for the files pane.
//
// Kept free of wx so it can be unit tested, and because the counting rule is
// the interesting part: a folder's count is the number of Markdown files
// anywhere beneath it, which is what lets the tree hide folders that contain
// no Markdown at all without concealing anything.

#ifndef MDBOSS_APP_FILE_SCAN_H
#define MDBOSS_APP_FILE_SCAN_H

#include <map>
#include <string>
#include <vector>

namespace mdboss {

// Recognised Markdown extensions, matching the Python app's is_markdown().
bool is_markdown(const std::string& name);

// Case-folded absolute path, used as the key of the count map so lookups do
// not depend on how a path was spelled.
std::string norm_path(const std::string& path);

// Recursive Markdown count for every folder at or below `root`.
//
// A single bottom-up walk lets each folder sum its own files plus its
// children's totals.  A folder *missing* from the result was never walked --
// a junction, or unreadable -- which the tree treats differently from a
// folder that really holds zero.
std::map<std::string, int> md_counts_for_root(const std::string& root);

// One entry of a directory listing, already ordered the way the tree shows
// them: directories first, then files, each case-insensitively by name.
struct Entry {
    std::string path;
    std::string name;
    bool is_dir = false;
};

std::vector<Entry> list_directory(const std::string& path);

// Delete to the Recycle Bin rather than unlinking, so a mis-click is
// recoverable -- the Python app uses Send2Trash for the same reason.
// Returns false if the shell refused or the user cancelled.
bool send_to_recycle_bin(const std::string& path);

// Which of several dropped files to open: the first Markdown one, else the
// first file at all.  The app asks before opening a non-Markdown file, so
// offering it beats a drop that silently does nothing.  Empty if the list is.
std::string choose_dropped_file(const std::vector<std::string>& filenames);

// What a file looked like at a moment in time.
//
// This is how the document watcher tells an edit by another program from the
// echo of our own save: writing a file raises exactly the same filesystem
// event that an outside edit does, and the only thing distinguishing them is
// whether the result is what we expected to be there.
struct FileStamp {
    bool exists = false;
    // The filesystem clock's raw tick count, opaque on purpose: it is only
    // ever compared with another stamp, never interpreted as a date.  Ticks
    // rather than seconds because NTFS resolves to 100ns and an editor can
    // easily write twice within one second without changing the length.
    long long mtime_ticks = 0;
    unsigned long long size = 0;
};

bool operator==(const FileStamp& a, const FileStamp& b);
bool operator!=(const FileStamp& a, const FileStamp& b);

// Never throws and never reports failure as a distinct state: a path that is
// missing, unreadable or unstattable all yield `exists == false`, because the
// caller's response to each is the same -- do not touch the open buffer.
FileStamp stamp_of(const std::string& path);

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_SCAN_H
