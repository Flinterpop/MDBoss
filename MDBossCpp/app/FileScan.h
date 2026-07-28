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

// The filename to actually create for a typed-in name.
//
// ".md" is appended only when there is NO extension, matching the Python
// app's `if os.path.splitext(name)[1] == ""`.  Appending whenever the name is
// not Markdown instead would turn "notes.txt" into "notes.txt.md", which is
// not the name the user typed.
std::string ensure_markdown_extension(const std::string& name);

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

// The optional landing folder for documents copied in via "Import files into
// MD_Inbox…".  Dropping a file does NOT copy here -- drops open the file
// where it lies -- so this name only governs the explicit import command.
inline constexpr const char* kInboxName = "MD_Inbox";

// The MD_Inbox folder among `root_paths`, or empty when there is none.
//
// A root may itself be named MD_Inbox, or hold one as a top-level subfolder.
// Matching is case-insensitive and the first match wins, as in the Python
// app's find_inbox().
std::string find_inbox(const std::vector<std::string>& root_paths);

// A path in `dest_dir` for `filename` that will not overwrite anything: on
// collision " (2)", " (3)" ... is inserted before the extension, so importing
// a file whose name is already taken never clobbers the one already there.
std::string unique_dest(const std::string& dest_dir,
                        const std::string& filename);

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
