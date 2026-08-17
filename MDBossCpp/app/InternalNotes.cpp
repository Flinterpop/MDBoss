#include "InternalNotes.h"

#include "FileScan.h"
#include "PathUtf8.h"

#include <cassert>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mdboss {
namespace {

namespace fs = std::filesystem;

// Trim both ends.  A field the user padded with spaces should not widen the
// table, and a to-do of nothing but whitespace is not a to-do.
std::string trimmed(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
            text[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin) {
        const char ch = text[end - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        --end;
    }
    return text.substr(begin, end - begin);
}

}  // namespace

std::string internal_folder(const std::vector<std::string>& root_paths)
{
    // Beside MD_Inbox when there is one: "the same folder as MD_Inbox" means a
    // sibling, so it is the inbox's PARENT that is joined with the name.
    const std::string inbox = find_inbox(root_paths);
    if (!inbox.empty()) {
        const fs::path parent =
            path_from_utf8(inbox).lexically_normal().parent_path();
        if (!parent.empty()) {
            return path_to_utf8(parent / path_from_utf8(kInternalName));
        }
    }

    // No inbox anywhere.  Fall back to the first root that is really a folder,
    // so the three commands work for someone who has never made an MD_Inbox
    // rather than failing with a message about a folder they never asked for.
    std::error_code ec;
    for (const std::string& root : root_paths) {   // bounded by the roots list
        if (root.empty()) {
            continue;
        }
        const fs::path dir = path_from_utf8(root);
        if (fs::is_directory(dir, ec) && !ec) {
            return path_to_utf8(dir / path_from_utf8(kInternalName));
        }
        ec.clear();
    }
    return {};
}

std::string escape_table_cell(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : trimmed(text)) {   // bounded by the field length
        if (ch == '|') {
            out += "\\|";        // else the cell ends here and columns shift
        } else if (ch == '\n' || ch == '\r') {
            out += ' ';          // else the row ends here
        } else {
            out += ch;
        }
    }
    return out;
}

std::string login_table_row(const LoginRecord& record)
{
    std::ostringstream row;
    row << "| " << escape_table_cell(record.name)
        << " | " << escape_table_cell(record.link)
        << " | " << escape_table_cell(record.login)
        << " | " << escape_table_cell(record.password)
        << " | " << escape_table_cell(record.last_changed)
        << " | " << escape_table_cell(record.notes)
        << " |\n";
    return row.str();
}

std::string todo_line(const std::string& item, const std::string& date)
{
    // An unticked GitHub task-list item, with the date appended so the list
    // reads as one line per to-do.  The item is flattened to a single line for
    // the same reason a table cell is: a newline here would end the list item
    // and orphan the rest of the text.
    std::string flat;
    flat.reserve(item.size());
    for (const char ch : trimmed(item)) {   // bounded by the item length
        flat += (ch == '\n' || ch == '\r') ? ' ' : ch;
    }
    std::string line = "- [ ] " + flat;
    if (!date.empty()) {
        line += " -- " + date;
    }
    line += "\n";
    return line;
}

std::string diary_entry(const std::string& markdown, const std::string& date)
{
    // A heading, so each entry is a real section: it lands in the Outline pane
    // and can be linked to.  Repeating a date is fine -- the renderer already
    // de-duplicates heading slugs, which the golden corpus covers.
    //
    // The body is NOT escaped or flattened: the whole point of this one is
    // that the user types Markdown and gets Markdown.
    std::ostringstream entry;
    entry << "\n## " << (date.empty() ? std::string("(undated)") : date)
          << "\n\n" << trimmed(markdown) << "\n";
    return entry.str();
}

std::string logins_seed()
{
    return "# Logins\n"
           "\n"
           "Managed by MD Boss (Lists menu). Rows may also be edited by hand.\n"
           "\n"
           "| Name | Link | Login | PW | Last Changed | Notes |\n"
           "|---|---|---|---|---|---|\n";
}

std::string todo_seed()
{
    // The trailing BLANK line matters: the first item is appended straight
    // after this, and a "- [ ]" line butted against the paragraph above it is
    // at best fragile and at worst swallowed into that paragraph rather than
    // starting a list.  The table seed needs no equivalent -- a table row
    // directly under the header separator is exactly right.
    return "# To Do\n"
           "\n"
           "Managed by MD Boss (Lists menu). Tick a box to mark it done.\n"
           "\n";
}

std::string diary_seed()
{
    return "# Grail Diary\n";
}

std::string internal_gitignore()
{
    // Deny everything, including this file: MD_Internal may sit inside a git
    // repo, logins.md holds plaintext passwords, and `git add -A` is one
    // keystroke.  Written once, when the folder is created.
    return "# Written by MD Boss.\n"
           "#\n"
           "# MD_Internal holds app-managed notes, including logins.md, which\n"
           "# stores passwords in plain text. Nothing here should ever be\n"
           "# committed. Delete this file only if you are certain.\n"
           "*\n";
}

std::string today_stamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
    // localtime_s, not localtime: the latter is deprecated under /W4 /WX and
    // returns a shared buffer.
    if (localtime_s(&parts, &now) != 0) {
        return {};
    }

    // Built by hand rather than with strftime: "%d" pads to two digits and
    // there is no portable way to ask it not to (MSVC's "%#d" is an extension
    // and glibc's "%-d" is another), while "%b" is locale-dependent -- on a
    // non-English Windows it would put a translated month into a file whose
    // format is meant to be fixed.  Three lines of table beats both.
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                          "May", "Jun", "Jul", "Aug",
                                          "Sep", "Oct", "Nov", "Dec"};
    if (parts.tm_mon < 0 || parts.tm_mon > 11) {
        return {};
    }
    std::ostringstream stamp;
    stamp << parts.tm_mday << ' ' << kMonths[parts.tm_mon] << ' '
          << (parts.tm_year + 1900);
    return stamp.str();
}

namespace {

// Make sure the folder exists and is guarded, and say what went wrong if not.
// Shared by both writers, because the guard must be in place before ANY file
// is written there -- not just before the first one.
std::string prepare_internal_folder(const std::string& folder)
{
    if (folder.empty()) {
        return "No folder is configured to keep " + std::string(kInternalName) +
               " in. Add a folder with Manage folders first.";
    }

    const fs::path dir = path_from_utf8(folder);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        ec.clear();
        fs::create_directories(dir, ec);
        if (ec) {
            return "Could not create " + folder + ": " + ec.message();
        }
    }
    ec.clear();

    // Checked on EVERY write, not only when this call created the folder.
    // Guarding it at creation alone left a real MD_Internal unguarded for over
    // an hour: the folder can arrive by other means -- made by hand, restored
    // from a backup, or synced in from another machine -- and then nothing
    // would ever write the guard, which is precisely when it is needed.
    const fs::path guard = dir / ".gitignore";
    if (!fs::exists(guard, ec)) {
        ec.clear();
        const std::string failed =
            write_text_file_checked(path_to_utf8(guard), internal_gitignore());
        if (!failed.empty()) {
            return failed;   // refuse rather than leave it unguarded
        }
    }
    return {};
}

}  // namespace

std::string write_internal_file(const std::string& folder,
                                const std::string& filename,
                                const std::string& text)
{
    assert(!filename.empty() && "a file needs a name");
    const std::string failed = prepare_internal_folder(folder);
    if (!failed.empty()) {
        return failed;
    }
    return write_text_file_checked(
        path_to_utf8(path_from_utf8(folder) / path_from_utf8(filename)), text);
}

std::string append_to_internal(const std::string& folder,
                               const std::string& filename,
                               const std::string& seed,
                               const std::string& block)
{
    assert(!filename.empty() && "a note file needs a name");
    assert(!block.empty() && "appending nothing is a caller bug");
    const std::string prepared = prepare_internal_folder(folder);
    if (!prepared.empty()) {
        return prepared;
    }

    const fs::path dir = path_from_utf8(folder);
    std::error_code ec;

    const fs::path file = dir / path_from_utf8(filename);
    std::string text;
    if (fs::is_regular_file(file, ec) && !ec) {
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            return "Could not read " + path_to_utf8(file);
        }
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        text = strip_utf8_bom(buffer.str());
    } else {
        text = seed;   // first use: title, and the table header if it has one
    }
    ec.clear();

    // Exactly one newline between what is there and what is added, so a file
    // whose last line lacks a terminator does not swallow the new entry.
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    text += block;

    return write_text_file_checked(path_to_utf8(file), text);
}

}  // namespace mdboss
