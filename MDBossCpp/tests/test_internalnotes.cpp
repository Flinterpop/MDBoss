// The MD_Internal lists: the formatting each command appends, and the append
// itself.
//
// The formatting is where this can go quietly wrong.  A stray '|' in a Notes
// field silently shifts every column after it, and a pasted multi-line value
// ends the row early -- both produce a file that still renders, just not as
// the table it was meant to be.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "InternalNotes.h"

namespace fs = std::filesystem;

namespace {

// A throwaway folder, removed when the fixture goes out of scope.
class TempDir {
public:
    TempDir()
    {
        root_ = fs::temp_directory_path() / "mdboss_internal_test";
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string path() const { return root_.string(); }
    std::string file(const char* name) const { return (root_ / name).string(); }

    std::string read(const char* name) const
    {
        std::ifstream stream(root_ / name, std::ios::binary);
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

private:
    fs::path root_;
};

}  // namespace

TEST_CASE("a pipe in a field cannot break the table", "[internal]")
{
    // The failure this guards is silent: an unescaped '|' ends the cell, so
    // every column after it shifts left and the row still renders.
    CHECK(mdboss::escape_table_cell("a|b") == "a\\|b");
    CHECK(mdboss::escape_table_cell("plain") == "plain");

    // A newline would end the ROW, orphaning the rest of the value.
    CHECK(mdboss::escape_table_cell("two\nlines") == "two lines");
    CHECK(mdboss::escape_table_cell("crlf\r\nhere") == "crlf  here");

    // Padding a field should not widen the column.
    CHECK(mdboss::escape_table_cell("  spaced  ") == "spaced");
}

TEST_CASE("a login row has one cell per column", "[internal]")
{
    mdboss::LoginRecord record;
    record.name = "Example";
    record.link = "https://example.test";
    record.login = "brad";
    record.password = "p|ssword";      // the awkward case, deliberately
    record.last_changed = "16 Aug 2026";
    record.notes = "note";

    const std::string row = mdboss::login_table_row(record);
    CHECK(row == "| Example | https://example.test | brad | p\\|ssword | "
                 "16 Aug 2026 | note |\n");

    // Six columns means seven separators, and the escaped pipe must not be
    // counted as one of them.
    std::size_t bars = 0;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (row[i] == '|' && (i == 0 || row[i - 1] != '\\')) {
            ++bars;
        }
    }
    CHECK(bars == 7);
}

TEST_CASE("the seeded header matches the row shape", "[internal]")
{
    // A header with a different column count than the rows renders as a
    // broken table, and nothing else would catch it.
    const std::string seed = mdboss::logins_seed();
    const std::size_t header = seed.find("| Name |");
    REQUIRE(header != std::string::npos);

    const auto columns = [](const std::string& line) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '|' && (i == 0 || line[i - 1] != '\\')) {
                ++count;
            }
        }
        return count;
    };
    const std::size_t end = seed.find('\n', header);
    REQUIRE(end != std::string::npos);
    CHECK(columns(seed.substr(header, end - header)) == 7);
}

TEST_CASE("the first to-do starts a list, not a paragraph", "[internal]")
{
    // Found by running the app, not by a test: the seed ended flush against
    // the first item, so "- [ ] ..." sat directly under a paragraph line.
    // A blank line between them is what makes it a list.
    const TempDir dir;
    REQUIRE(mdboss::append_to_internal(dir.path(), mdboss::kTodoFile,
                                       mdboss::todo_seed(),
                                       mdboss::todo_line("first", "16 Aug 2026"))
                .empty());
    const std::string text = dir.read(mdboss::kTodoFile);
    const std::size_t item = text.find("- [ ] first");
    REQUIRE(item != std::string::npos);
    REQUIRE(item >= 2);
    CHECK(text.substr(item - 2, 2) == "\n\n");
}

TEST_CASE("a to-do is one tickable line", "[internal]")
{
    CHECK(mdboss::todo_line("Ring the supplier", "16 Aug 2026") ==
          "- [ ] Ring the supplier -- 16 Aug 2026\n");

    // Flattened for the same reason a cell is: a newline ends the list item.
    CHECK(mdboss::todo_line("two\nlines", "16 Aug 2026") ==
          "- [ ] two lines -- 16 Aug 2026\n");

    // No date is still a valid item, not an item with a dangling separator.
    CHECK(mdboss::todo_line("undated", "") == "- [ ] undated\n");
}

TEST_CASE("a diary entry is a dated section with its markdown intact",
          "[internal]")
{
    // Unlike the other two, NOTHING here may be escaped or flattened: the
    // whole point is that the user types Markdown and gets Markdown.
    const std::string entry =
        mdboss::diary_entry("Found the **cup**.\n\n- one\n- two", "16 Aug 2026");
    CHECK(entry == "\n## 16 Aug 2026\n\nFound the **cup**.\n\n- one\n- two\n");
}

TEST_CASE("a fact is one table row, every cell escaped", "[internal]")
{
    mdboss::FactRecord record;
    record.date = "11 Apr 2023";
    record.fact = "The mask angle is measured from the horizontal";
    record.tags = "radar, survey";
    record.source = "site notes";

    CHECK(mdboss::fact_table_row(record) ==
          "| 11 Apr 2023 | The mask angle is measured from the horizontal "
          "| radar, survey | site notes |\n");

    // Every cell, INCLUDING the date -- it is prefilled but editable, so it is
    // user input like the rest, and a pipe there shifts the three columns
    // after it.  A newline would end the row outright.
    mdboss::FactRecord hostile;
    hostile.date = "a|b";
    hostile.fact = "one\ntwo";
    hostile.tags = "x|y";
    hostile.source = "p|q";
    const std::string row = mdboss::fact_table_row(hostile);
    CHECK(row == "| a\\|b | one two | x\\|y | p\\|q |\n");
    CHECK(row.find('\n') == row.size() - 1);
}

TEST_CASE("the facts seed carries the table header", "[internal]")
{
    const std::string seed = mdboss::facts_seed();
    CHECK(seed.find("# Facts") != std::string::npos);
    CHECK(seed.find("| Date | Fact | Tags | Source |") != std::string::npos);
    CHECK(seed.find("|---|---|---|---|") != std::string::npos);
    // The one thing about this file that cannot be inferred by looking at it.
    CHECK(seed.find("when the fact is true of") != std::string::npos);
    // Ends ready for a row: no blank line between the header and the first
    // entry, or the table is closed before anything is added to it.
    CHECK(seed.substr(seed.size() - 2) == "|\n");
}

TEST_CASE("appending seeds once, then only adds", "[internal]")
{
    const TempDir dir;
    const std::string folder = dir.path();

    mdboss::LoginRecord first;
    first.name = "One";
    REQUIRE(mdboss::append_to_internal(folder, mdboss::kLoginsFile,
                                       mdboss::logins_seed(),
                                       mdboss::login_table_row(first))
                .empty());

    mdboss::LoginRecord second;
    second.name = "Two";
    REQUIRE(mdboss::append_to_internal(folder, mdboss::kLoginsFile,
                                       mdboss::logins_seed(),
                                       mdboss::login_table_row(second))
                .empty());

    const std::string text = dir.read(mdboss::kLoginsFile);
    // Seeded exactly once: re-seeding would put a second header mid-table.
    CHECK(text.find("| Name |") != std::string::npos);
    CHECK(text.find("| Name |") == text.rfind("| Name |"));
    // Both rows present, in the order they were added.
    CHECK(text.find("| One |") < text.find("| Two |"));
}

TEST_CASE("creating the folder writes the gitignore first", "[internal]")
{
    // MD_Internal may sit inside a git repo and logins.md holds plaintext
    // passwords, so the guard has to exist from the moment the folder does --
    // not the next time something happens to write it.
    const TempDir dir;
    REQUIRE(mdboss::append_to_internal(dir.path(), mdboss::kTodoFile,
                                       mdboss::todo_seed(),
                                       mdboss::todo_line("x", "16 Aug 2026"))
                .empty());

    REQUIRE(fs::is_regular_file(dir.file(".gitignore")));
    const std::string guard = dir.read(".gitignore");
    // Deny-by-default, or a new file added later slips out from under it.
    CHECK(guard.find("\n*\n") != std::string::npos);
}

TEST_CASE("an existing folder still gets the gitignore", "[internal]")
{
    // Found in the wild: a real MD_Internal sat unguarded for over an hour
    // because the guard was written only when append_to_internal() had created
    // the folder itself.  The folder can arrive by other means -- made by
    // hand, restored, or synced in from another machine -- and that is exactly
    // when nobody has written a guard.
    const TempDir dir;
    std::error_code ec;
    fs::create_directories(dir.path(), ec);   // exists, with no guard
    REQUIRE_FALSE(fs::is_regular_file(dir.file(".gitignore")));

    REQUIRE(mdboss::append_to_internal(dir.path(), mdboss::kTodoFile,
                                       mdboss::todo_seed(),
                                       mdboss::todo_line("x", "16 Aug 2026"))
                .empty());

    CHECK(fs::is_regular_file(dir.file(".gitignore")));
}

TEST_CASE("a file without a trailing newline still gains a separate entry",
          "[internal]")
{
    // Hand-edited files are expected here, and an editor that strips the final
    // newline would otherwise run the next entry onto the last line.
    const TempDir dir;
    std::error_code ec;
    fs::create_directories(dir.path(), ec);
    {
        std::ofstream stream(dir.file(mdboss::kTodoFile), std::ios::binary);
        stream << "# To Do\n\n- [ ] existing";   // no terminator
    }

    REQUIRE(mdboss::append_to_internal(dir.path(), mdboss::kTodoFile,
                                       mdboss::todo_seed(),
                                       mdboss::todo_line("added", "16 Aug 2026"))
                .empty());

    const std::string text = dir.read(mdboss::kTodoFile);
    CHECK(text.find("- [ ] existing\n- [ ] added") != std::string::npos);
}

TEST_CASE("today reads as day, month, year", "[internal]")
{
    // "16 Aug 2026": day with no leading zero, three-letter month, four-digit
    // year -- this repo's documented date format, which rules out ISO and
    // numeric locale forms.  The three commands share it, so a drift shows up
    // here rather than as three files that disagree.
    const std::string today = mdboss::today_stamp();

    std::istringstream parts(today);
    int day = 0;
    std::string month;
    int year = 0;
    REQUIRE((parts >> day >> month >> year));

    CHECK(day >= 1);
    CHECK(day <= 31);
    CHECK(month.size() == 3);
    CHECK(year >= 2026);

    // No leading zero, and nothing left over after the year.
    CHECK(today[0] != '0');
    std::string trailing;
    CHECK_FALSE((parts >> trailing));

    // Three fields, so exactly two spaces -- not a numeric or ISO form.
    CHECK(std::count(today.begin(), today.end(), ' ') == 2);
    CHECK(today.find('-') == std::string::npos);
    CHECK(today.find('/') == std::string::npos);

    // The month must be a real abbreviation, not a locale translation: a
    // non-English Windows would otherwise put a different word in the file.
    const std::string names = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";
    CHECK(names.find(month) != std::string::npos);
}

TEST_CASE("no roots means a refusal, not a write somewhere random",
          "[internal]")
{
    const std::string failed = mdboss::internal_folder({});
    CHECK(failed.empty());

    const std::string message = mdboss::append_to_internal(
        "", mdboss::kTodoFile, mdboss::todo_seed(), "- [ ] x\n");
    CHECK_FALSE(message.empty());
}
