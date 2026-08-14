// Filesystem scanning for the files pane.
//
// Kept free of wx so it can be unit tested, and because the counting rule is
// the interesting part: a folder's count is the number of Markdown files
// anywhere beneath it, which is what lets the tree hide folders that contain
// no Markdown at all without concealing anything.

#ifndef MDBOSS_APP_FILE_SCAN_H
#define MDBOSS_APP_FILE_SCAN_H

#include <cstddef>
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
