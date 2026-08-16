// The three app-managed lists that live in MD_Internal: saved logins, a to-do
// checklist, and the Grail Diary.
//
// MD_Internal sits BESIDE MD_Inbox, in the same parent folder, and holds
// ordinary Markdown -- nothing here is hidden from the files tree, the folder
// counts or the Contents search.  These are just three commands that append a
// well-formed block to a known file, and the files stay perfectly editable by
// hand afterwards.
//
// Everything except append_to_internal() is a pure function over strings, so
// the formatting is unit-tested without touching a disk.  Nothing in this file
// includes wxWidgets, which is what lets the test binary compile it directly.
//
// A NOTE ON logins.md.  It stores passwords as plaintext Markdown inside one
// of the user's document roots, which was a deliberate choice (2026-08-16).
// Two consequences follow, and both are handled here rather than left to
// chance: MD_Internal gets a .gitignore the moment it is created, because a
// document root may sit inside a git repo -- two of this author's are public
// -- and the file is otherwise one `git add -A` from being world-readable;
// and the content is indexed by the Contents search like any other document,
// so a password can surface in a search result. The second is inherent to the
// choice and is documented rather than defended against.

#ifndef MDBOSS_APP_INTERNAL_NOTES_H
#define MDBOSS_APP_INTERNAL_NOTES_H

#include <string>
#include <vector>

namespace mdboss {

// The folder, and the three files inside it.  Named here rather than spelled
// at each call site so a rename cannot reach only some of them.
inline constexpr const char* kInternalName = "MD_Internal";
inline constexpr const char* kLoginsFile = "logins.md";
inline constexpr const char* kTodoFile = "ToDoList.md";
inline constexpr const char* kDiaryFile = "GrailDiary.md";

// Where MD_Internal belongs, given the configured roots.  It is the sibling of
// MD_Inbox -- same parent folder -- so the two app-managed folders sit
// together.  When no MD_Inbox exists anywhere, the first usable root is used
// instead, so the commands still work for someone who has never made one.
// Empty only when there are no usable roots at all.
//
// This returns where the folder BELONGS; it need not exist yet.
std::string internal_folder(const std::vector<std::string>& root_paths);

// One row of logins.md.  `last_changed` is a date string, not parsed here.
struct LoginRecord {
    std::string name;
    std::string link;
    std::string login;
    std::string password;
    std::string last_changed;
    std::string notes;
};

// A cell that cannot break the table it is put in: a literal '|' would end the
// cell early and silently shift every column after it, and a newline would end
// the row.  Pipes are escaped and newlines become spaces.
std::string escape_table_cell(const std::string& text);

// The block each command appends.  Each ends with exactly one newline, so
// appending several in a row cannot run two entries together or leave a
// widening gap.
std::string login_table_row(const LoginRecord& record);
std::string todo_line(const std::string& item, const std::string& date);
std::string diary_entry(const std::string& markdown, const std::string& date);

// What a file gets when it does not exist yet: a title, and for logins the
// table header the rows hang off.  A file the user has since edited by hand is
// never re-seeded -- seeding happens only when there is nothing there.
std::string logins_seed();
std::string todo_seed();
std::string diary_seed();

// The .gitignore written into MD_Internal when it is created.  See the note at
// the top of this file.
std::string internal_gitignore();

// Append `block` to `folder`/`filename`, creating the folder, its .gitignore
// and the file (with `seed`) as needed.  New entries always go at the END of
// the file.
//
// Returns an empty string on success, otherwise a sentence naming what failed
// -- the caller must treat a non-empty return as the entry NOT having been
// saved.  The write goes through write_text_file_checked(), so a partly-freed
// buffer cannot destroy a file that already holds real entries.
std::string append_to_internal(const std::string& folder,
                               const std::string& filename,
                               const std::string& seed,
                               const std::string& block);

// Today as YYYY-MM-DD, local time.  Here rather than at the call sites so all
// three commands agree, and so a test can compare against a fixed format.
std::string today_iso();

}  // namespace mdboss

#endif  // MDBOSS_APP_INTERNAL_NOTES_H
