// Finding the tech notes under the configured roots, and writing the index of
// them into MD_Internal.
//
// A document is a tech note only if its front matter carries BOTH a `GUID:`
// and `TechNote` among its `keywords:` (user ruling, 2026-08-17).  The keyword
// says what the document is; the GUID says which one it is.  Requiring both is
// what stops a stray GUID in some unrelated document turning it into a tech
// note, and it means removing the keyword deliberately takes a note off the
// list.
//
// The index is DERIVED, never accumulated: it is rebuilt by reading the
// documents, so a note created in another editor, renamed, moved or deleted
// all come out right, and notes that existed before this feature did are
// picked up on the first run.  A registry appended to as notes are created
// would be cheaper and would start drifting the moment anything happened
// outside MD Boss, with nothing able to correct it.
//
// Everything except scan_tech_notes() is a pure function over strings, so the
// parsing and the index text are unit-tested without touching a disk, and
// nothing here includes wxWidgets.

#ifndef MDBOSS_APP_TECH_NOTES_H
#define MDBOSS_APP_TECH_NOTES_H

#include <string>
#include <string_view>
#include <vector>

namespace mdboss {

// The file the index is written to, inside MD_Internal beside the other lists.
inline constexpr const char* kTechNotesFile = "TechNotes.md";

// Ceilings on the work a rebuild does (Rule of 10).  Front matter is always at
// the very top of a file, so there is no reason to read further than the head
// of one -- which is what keeps a rebuild affordable over a root holding
// thousands of documents.
inline constexpr std::size_t kMaxHeadBytes = 4096;
inline constexpr std::size_t kMaxNotesExamined = 20000;
inline constexpr std::size_t kMaxNotesListed = 5000;

struct TechNote {
    std::string path;      // absolute, UTF-8
    std::string title;
    std::string guid;
    std::string version;
    std::string subject;
};

// Parse the head of a document.  Returns false when it is not a tech note --
// no front matter, no GUID, or no TechNote keyword -- in which case `out` is
// not written to.
//
// `text` may be only the first few KB of the file; front matter that has not
// been closed within that is treated as absent rather than guessed at.
bool parse_tech_note(std::string_view text, TechNote* out);

// Every tech note among `paths`, sorted by title (case-insensitively, then by
// path so the order is total and stable).  Unreadable files are skipped
// silently: one of them is not worth interrupting a rebuild for.
std::vector<TechNote> scan_tech_notes(const std::vector<std::string>& paths);

// The index document.  `generated` is a date stamp shown in the text, since a
// derived file that does not say when it was derived invites being trusted
// after it has gone stale.  `roots` shortens each path for display; a note
// outside every root keeps its absolute path.
std::string tech_notes_index(const std::vector<TechNote>& notes,
                             const std::vector<std::string>& roots,
                             const std::string& generated);

}  // namespace mdboss

#endif  // MDBOSS_APP_TECH_NOTES_H
