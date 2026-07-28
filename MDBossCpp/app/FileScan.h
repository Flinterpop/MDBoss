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

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_SCAN_H
