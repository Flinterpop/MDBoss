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

// A tech note's number: the year, a dot, and a sequence that restarts each
// year -- 2026.01.  Two digits is the minimum rather than the maximum, so a
// year that runs past 99 notes gets 2026.100 instead of a number that collides
// with an earlier one.
//
// The number is allocated by DERIVING it, exactly as the index is: the highest
// sequence already in use for that year, plus one.  A counter kept in config
// would be one number, in one profile, that nothing could correct -- it would
// drift the first time a note was written elsewhere, deleted, or restored from
// a backup, and the drift would show up as two notes claiming one number.
inline constexpr const char* kTnIndexPlaceholder = "{{tnindex}}";

// Ceiling on the sequence (Rule of 10).  Far beyond any real year's output; it
// exists so a corrupt or hostile number cannot make the next one run away.
inline constexpr int kMaxTnSequence = 9999;

struct TechNote {
    std::string path;      // absolute, UTF-8
    std::string title;
    std::string guid;
    std::string version;
    std::string subject;
    // Added after the fact, so it goes last: notes written before this
    // existed have none, and an aggregate initializer that predates it still
    // compiles.
    std::string tn_index;
};

// Parse the head of a document.  Returns false when it is not a tech note --
// no front matter, no GUID, or no TechNote keyword -- in which case `out` is
// not written to.
//
// `text` may be only the first few KB of the file; front matter that has not
// been closed within that is treated as absent rather than guessed at.
bool parse_tech_note(std::string_view text, TechNote* out);

// Every tech note among `paths`, in number order -- numbered notes by year
// then sequence, then the unnumbered ones, each group by title
// (case-insensitively, then by path so the order is total and stable).
// Unreadable files are skipped silently: one of them is not worth interrupting
// a rebuild for.
std::vector<TechNote> scan_tech_notes(const std::vector<std::string>& paths);

// The index document.  `generated` is a date stamp shown in the text, since a
// derived file that does not say when it was derived invites being trusted
// after it has gone stale.  `roots` shortens each path for display; a note
// outside every root keeps its absolute path.
std::string tech_notes_index(const std::vector<TechNote>& notes,
                             const std::vector<std::string>& roots,
                             const std::string& generated);

// ---- Numbering a new note -------------------------------------------------

// `year`.`sequence`, zero-padded to two digits.  Asserts on a sequence outside
// 1..kMaxTnSequence, which no caller here can produce.
std::string format_tn_index(int year, int sequence);

// Split a number back apart.  False for anything that is not exactly four
// digits, a dot, and one to four digits -- a note whose TNIndex was hand-typed
// into something else is simply not part of the numbered series, which is
// safer than guessing a sequence out of it and colliding with a real one.
bool parse_tn_index(const std::string& text, int* year, int* sequence);

// The next free sequence for `year`: one past the highest already in use.
// Notes from other years are ignored, so each year starts again at 1.
int next_tn_sequence(const std::vector<TechNote>& existing, int year);

// True when `text` carries the placeholder, i.e. this template wants a number.
//
// Checked before scanning, so that creating a document from any other template
// stays free: the scan reads the head of every document under every root, and
// paying that for a template with no placeholder in it would be pure waste.
bool needs_tn_index(const std::string& text);

// ---- Turning an ordinary document into a tech note ------------------------

// What `text` is missing before it would count as a tech note.  All false
// means it already is one.
struct TechNoteGaps {
    bool front_matter = false;   // no closed block at the very top
    bool guid = false;
    bool keyword = false;
    bool tn_index = false;
};
TechNoteGaps tech_note_gaps(const std::string& text);

// `text` with `GUID:`, `TNIndex:` and the `TechNote` keyword added where they
// are missing, and a front-matter block written when there is none.
//
// Only ever ADDS.  A GUID, number or keyword already there is left exactly as
// it is: this is a document the user wrote, and the command was "make this a
// tech note", not "renumber it".  `title` is used only when a whole block has
// to be written; pass the document's filename stem.
//
// Returns `text` unchanged when nothing is missing.
std::string promote_to_tech_note(const std::string& text,
                                 const std::string& guid,
                                 const std::string& tn_index,
                                 const std::string& title);

// The year a document belongs to: a four-digit year leading a `date:`,
// `created:` or `updated:` value in its front matter, or `fallback_year` when
// there is none.  A note written years ago should be numbered in the year it
// was written, not the year someone got round to indexing it.
int tech_note_year(const std::string& text, int fallback_year);

// `text` with the placeholder replaced by the next number for `year`.
//
// `assigned`, when given, receives the number that was handed out -- so the
// caller can treat it as taken before the note holding it has been saved.  It
// is left untouched when there was no placeholder to fill.
std::string fill_tn_index(const std::string& text,
                          const std::vector<TechNote>& existing, int year,
                          std::string* assigned = nullptr);

}  // namespace mdboss

#endif  // MDBOSS_APP_TECH_NOTES_H
