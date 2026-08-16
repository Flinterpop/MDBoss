// Filesystem scanning for the files pane.
//
// Kept free of wx so it can be unit tested, and because the counting rule is
// the interesting part: a folder's count is the number of Markdown files
// anywhere beneath it, which is what lets the tree hide folders that contain
// no Markdown at all without concealing anything.

#ifndef MDBOSS_APP_FILE_SCAN_H
#define MDBOSS_APP_FILE_SCAN_H

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mdboss {

// Recognised Markdown extensions, matching the Python app's is_markdown().
bool is_markdown(const std::string& name);

// Case-folded absolute path, used as the key of the count map so lookups do
// not depend on how a path was spelled.
std::string norm_path(const std::string& path);

// `text` without a leading UTF-8 byte-order mark.
//
// Notepad and most Windows editors write one, and a leading U+FEFF stops
// "# Heading" being a heading -- the document renders as plain text -- and
// makes a JSON parser reject the file outright.  Python's utf-8-sig codec
// does this; C++ has no equivalent, so every read of user content goes
// through here.  Saving writes plain UTF-8, so a round trip drops the mark
// rather than preserving it.
std::string strip_utf8_bom(std::string text);

// Offset of the first byte at which `text` stops being well-formed UTF-8
// document text, or std::string::npos when the whole string is clean.  NUL
// bytes count as invalid: they are structurally legal UTF-8 but never legal
// Markdown, and the corruption that shipped in v1.2.0 -- a save that wrote a
// buffer whose first 16 bytes the heap had reclaimed as freed-block links --
// always announces itself with them.
std::size_t first_invalid_utf8(std::string_view text);

// What a raw file's bytes look like, decided cheaply and conservatively.
// kBinary means "no safe interpretation exists" -- the caller must refuse
// the file rather than guess.
enum class TextEncoding { kUtf8, kUtf16LE, kUtf16BE, kCp1252, kBinary };

TextEncoding detect_text_encoding(const std::string& bytes);

// Name for dialogs, e.g. "UTF-16 (little-endian)".
std::string text_encoding_name(TextEncoding encoding);

// `bytes` re-encoded as UTF-8 according to `encoding`, any BOM dropped.
// `ok` is false when the bytes do not actually decode under that encoding;
// the return value is then empty and must not be used.
std::string convert_to_utf8(const std::string& bytes, TextEncoding encoding,
                            bool& ok);

// Whole-file write with the two checks a bare ofstream lacks: the buffer
// must be clean UTF-8 before the write (a corrupted in-memory buffer must
// never destroy the file on disk), and the file is re-read and compared
// afterwards so a failed or mangled write cannot pass silently.  Returns an
// empty string on success, otherwise a sentence describing what was refused
// -- on any failure the caller must treat the save as not having happened.
std::string write_text_file_checked(const std::string& path,
                                    const std::string& text);

// The filename to actually create for a typed-in name.
//
// ".md" is appended only when there is NO extension, matching the Python
// app's `if os.path.splitext(name)[1] == ""`.  Appending whenever the name is
// not Markdown instead would turn "notes.txt" into "notes.txt.md", which is
// not the name the user typed.
std::string ensure_markdown_extension(const std::string& name);

// A filename for a document titled `title`, or empty when the title yields
// nothing usable to offer.
//
// Only ever a *suggestion* for the Save dialog, which is why an unusable title
// returns empty rather than something invented: an empty dialog is honest,
// a made-up name is not.  The result is a bare filename with the ".md"
// extension, never a path.
//
// Windows decides what is unusable: the reserved characters < > : " / \ | ? *,
// control characters, a trailing dot or space (which the shell silently
// strips, so a file would not have the name it appears to), and the reserved
// device names CON, PRN, AUX, NUL, COM1-9 and LPT1-9.  Markdown emphasis and
// code markers are dropped too, so "# The `parse()` **rule**" offers
// "The parse() rule.md" rather than the punctuation soup.
std::string filename_from_title(const std::string& title);

// A complete Markdown image reference for `image_path`, written for a document
// that lives at `document_path`.
//
// The path is made relative to the document's folder whenever the two share a
// drive, because that is what survives the pair being moved or committed
// together; across drives, and for a document not yet saved anywhere
// (`document_path` empty), only an absolute path can work.  Separators come
// out as forward slashes either way -- Markdown destinations are URLs, and
// backslashes are escapes there.
//
// A destination containing a space or a bracket is wrapped in <>, the
// CommonMark form for exactly that, rather than percent-encoded: it stays
// readable and hand-editable, which a path in a document usually has to be.
// The alt text is the file's stem.
std::string markdown_image_link(const std::string& image_path,
                                const std::string& document_path);

// True if `path` names something at or below any folder in `roots`.
//
// Case-insensitive via norm_path, and boundary-aware: "C:\Docs2\a.md" is not
// under "C:\Docs", which a bare prefix test would get wrong.  A trailing
// separator on a root does not change the answer.
//
// An empty `roots` yields false, which is the right reading rather than a
// degenerate one: with no roots configured there is no tree, so every open
// document is outside it.
bool is_under_any_root(const std::string& path,
                       const std::vector<std::string>& roots);

// One file whose text matched a content search.
struct ContentMatch {
    std::string path;
    // 1-based line number of the FIRST match, and that line's text, trimmed
    // and length-capped.  Only the first: finding every hit in every file
    // costs more than it tells you when the answer is "open this one".
    int line = 0;
    std::string text;
};

// Ceilings for a content search (Rule of 10 -- every one of these is a bound
// on work done while the user waits).  Public so the caller can say what it
// skipped rather than appear to have searched everything.
inline constexpr std::size_t kMaxSearchFileBytes = 1u << 20;   // 1 MiB
inline constexpr std::size_t kMaxSearchFiles = 20000;
inline constexpr std::size_t kMaxSearchResults = 500;
// Below this a content search matches so much that the result is noise.
inline constexpr std::size_t kMinSearchNeedle = 2;

// Markdown files at or below `root` whose text contains `needle`, matched
// case-insensitively over bytes.
//
// Never throws and never reports a per-file failure: an unreadable file is
// simply not a match, because the caller's response either way is the same.
// `stop` is polled between files so a search the user has already typed past
// can be abandoned; pass an empty function to run to completion.
std::vector<ContentMatch> search_file_contents(
    const std::string& root, const std::string& needle,
    const std::function<bool()>& stop = {});

// One Markdown file found beneath a root.
struct DocEntry {
    std::string path;          // absolute, UTF-8
    std::string name;          // leaf filename, UTF-8
    std::string relative_dir;  // "" directly in the root; '/'-separated, UTF-8
    // Which configured root this came from.  scan_root() knows nothing about
    // the roots list and always leaves this 0; the caller fills it in.
    std::size_t root_index = 0;
};

// Ceiling on documents recorded for one root (Rule of 10).  The tree builds an
// item per entry, so this bounds both memory and the work of a rebuild; a root
// past it is truncated rather than allowed to stall the UI thread.
inline constexpr std::size_t kMaxEntriesPerRoot = 100000;

// Every Markdown file at or below `root`, and the recursive Markdown count for
// every folder walked.
//
// ONE walk answers both, which is the point: the tree needs the files to build
// itself and the counts to label its folders, and walking twice for those was
// measurably slow over a few thousand files.  A single bottom-up roll-up lets
// each folder sum its own files plus its children's totals.
//
// A folder missing from `counts` was never walked -- a junction, or
// unreadable.  It contributes no entries either, so it simply does not appear
// in the tree: with nothing listing directories independently of this walk,
// there is no longer a way to show a folder the walk could not read.
//
// `truncated` says the walk stopped before the end of the tree, and exists
// because it once did so silently.  A workspace root of ~20 repos came to
// 1.76M entries against a 100,000 bound, 93% of it one app's tile cache: the
// walk gave up a fifth of the way through the alphabet and every repo after
// that simply was not in the tree, with nothing on screen to say so.  A bound
// that can be hit has to be reportable -- see kMaxWalkedEntries.
struct RootScan {
    std::vector<DocEntry> entries;
    std::map<std::string, int> counts;
    bool truncated = false;
    // Folders the walk was told to skip and did, absolute and UTF-8.  Reported
    // rather than merely obeyed: a pruned folder that leaves no trace in the
    // tree looks exactly like one that is empty, and the row is also the only
    // place the user can put it back.
    std::vector<std::string> excluded_folders;
};

// `excluded` folders are pruned: matched on the normalised absolute path, not
// descended into, and reported back in RootScan::excluded_folders.  Excluding
// one big generated folder (a build tree, a tile cache) is what keeps a scan
// of a whole workspace quick.  A built-in list of names to skip does not work:
// the folder that costs the most is as likely to be an app's own output
// directory, named whatever its author chose, as it is to be "node_modules".
RootScan scan_root(const std::string& root,
                   const std::vector<std::string>& excluded = {});

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
