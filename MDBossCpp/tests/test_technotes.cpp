// Finding tech notes, and the index built from them.
//
// The detection rule is the part worth pinning: BOTH a GUID and the TechNote
// keyword, so a stray GUID in an unrelated document does not turn it into a
// tech note and removing the keyword deliberately takes one off the list.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TechNotes.h"

namespace {

namespace fs = std::filesystem;

void write_note(const fs::path& path, const std::string& title,
                const std::string& index)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << "---\ntitle: " << title << "\nGUID: g-" << title << "\n";
    if (!index.empty()) {
        stream << "TNIndex: " << index << "\n";
    }
    stream << "keywords: TechNote\n---\n\n# " << title << "\n";
}

std::string note(const std::string& guid, const std::string& keywords,
                 const std::string& title = "A Note")
{
    return "---\ntitle: " + title +
           "\nauthor: B. Graham\nversion: 0.1\nGUID: " + guid +
           "\nkeywords: " + keywords + "\n---\n\n# " + title + "\n";
}

}  // namespace

TEST_CASE("a tech note needs BOTH a GUID and the keyword", "[technotes]")
{
    mdboss::TechNote out;

    CHECK(mdboss::parse_tech_note(note("abc-123", "TechNote"), &out));

    // Each alone is not enough.  This is the whole rule.
    CHECK_FALSE(mdboss::parse_tech_note(note("abc-123", "Draft"), &out));
    CHECK_FALSE(mdboss::parse_tech_note(note("", "TechNote"), &out));

    // No front matter at all.
    CHECK_FALSE(mdboss::parse_tech_note("# Just a heading\n", &out));
    // Front matter that is never closed is treated as absent rather than
    // guessed at -- the head we are given may simply have stopped early.
    CHECK_FALSE(mdboss::parse_tech_note("---\nGUID: x\nkeywords: TechNote\n",
                                        &out));
}

TEST_CASE("the fields come out of the front matter", "[technotes]")
{
    mdboss::TechNote out;
    REQUIRE(mdboss::parse_tech_note(
        "---\ntitle: Antenna Alignment\nversion: 0.3\nsubject: Survey\n"
        "GUID: 74e9da9d-8f16-4ad6-b927-3dfb8b60bf63\nkeywords: TechNote\n---\n",
        &out));
    CHECK(out.title == "Antenna Alignment");
    CHECK(out.version == "0.3");
    CHECK(out.subject == "Survey");
    CHECK(out.guid == "74e9da9d-8f16-4ad6-b927-3dfb8b60bf63");
}

TEST_CASE("the keyword is matched per entry, case-insensitively",
          "[technotes]")
{
    mdboss::TechNote out;
    // One of several keywords counts, in any case, with any spacing.
    CHECK(mdboss::parse_tech_note(note("g", "Draft, technote, Radar"), &out));
    CHECK(mdboss::parse_tech_note(note("g", "TECHNOTE"), &out));
    CHECK(mdboss::parse_tech_note(note("g", "  TechNote  "), &out));
    // But a different word that merely contains it does not.
    CHECK_FALSE(mdboss::parse_tech_note(note("g", "TechNotes"), &out));
    CHECK_FALSE(mdboss::parse_tech_note(note("g", "NotTechNote"), &out));
}

TEST_CASE("GUID is found whatever case the key is written in", "[technotes]")
{
    // The template writes GUID; YAML keys are conventionally lower case, and a
    // note hand-edited to `guid:` is still the same note.
    mdboss::TechNote out;
    CHECK(mdboss::parse_tech_note(
        "---\nguid: abc\nkeywords: TechNote\n---\n", &out));
    CHECK(out.guid == "abc");
}

TEST_CASE("an indented key belongs to something else", "[technotes]")
{
    // Only column zero is the document's own front matter; a nested mapping
    // that happens to contain a GUID must not promote the file.
    mdboss::TechNote out;
    CHECK_FALSE(mdboss::parse_tech_note(
        "---\nmeta:\n  GUID: abc\n  keywords: TechNote\n---\n", &out));
}

TEST_CASE("the TN index is read out of the front matter", "[technotes]")
{
    mdboss::TechNote out;
    REQUIRE(mdboss::parse_tech_note(
        "---\nGUID: g\nTNIndex: 2026.07\nkeywords: TechNote\n---\n", &out));
    CHECK(out.tn_index == "2026.07");

    // A note that predates the numbering is still a tech note; it simply has
    // no number.  Requiring one would silently drop every existing note.
    REQUIRE(mdboss::parse_tech_note("---\nGUID: g\nkeywords: TechNote\n---\n",
                                    &out));
    CHECK(out.tn_index.empty());
}

TEST_CASE("a TN index is the year, a dot, and two digits", "[technotes]")
{
    CHECK(mdboss::format_tn_index(2026, 1) == "2026.01");
    CHECK(mdboss::format_tn_index(2026, 9) == "2026.09");
    CHECK(mdboss::format_tn_index(2026, 42) == "2026.42");
    // Two digits is a floor, not a width: truncating here would hand out a
    // number an earlier note already has.
    CHECK(mdboss::format_tn_index(2026, 100) == "2026.100");
}

TEST_CASE("only a well-formed number parses", "[technotes]")
{
    int year = 0;
    int sequence = 0;
    REQUIRE(mdboss::parse_tn_index("2026.01", &year, &sequence));
    CHECK(year == 2026);
    CHECK(sequence == 1);
    CHECK(mdboss::parse_tn_index(" 2026.100 ", &year, &sequence));
    CHECK(sequence == 100);

    // Anything else is simply not part of the series.  Guessing a sequence out
    // of it would eventually collide with a real one.
    CHECK_FALSE(mdboss::parse_tn_index("", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("2026", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("2026.", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("2026.00", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("26.01", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("2026-01", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("2026.0a", &year, &sequence));
    CHECK_FALSE(mdboss::parse_tn_index("2026.12345", &year, &sequence));
}

TEST_CASE("the next number is one past the highest that year", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    // An empty year starts at 1.
    CHECK(mdboss::next_tn_sequence(notes, 2026) == 1);

    notes.push_back(mdboss::TechNote{"a.md", "A", "g", "", "", "2026.01"});
    notes.push_back(mdboss::TechNote{"c.md", "C", "g", "", "", "2026.03"});
    // Deliberately NOT one past the count: 2026.02 may have been deleted here
    // and still be cited somewhere else, so its number is not reused.
    CHECK(mdboss::next_tn_sequence(notes, 2026) == 4);

    // Each year counts on its own.
    CHECK(mdboss::next_tn_sequence(notes, 2027) == 1);
    notes.push_back(mdboss::TechNote{"o.md", "O", "g", "", "", "2025.99"});
    CHECK(mdboss::next_tn_sequence(notes, 2026) == 4);

    // Unnumbered and malformed notes are ignored rather than counted.
    notes.push_back(mdboss::TechNote{"u.md", "U", "g", "", "", ""});
    notes.push_back(mdboss::TechNote{"m.md", "M", "g", "", "", "TN-2026-09"});
    CHECK(mdboss::next_tn_sequence(notes, 2026) == 4);
}

TEST_CASE("the placeholder is filled, and only when present", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"a.md", "A", "g", "", "", "2026.06"});

    CHECK(mdboss::needs_tn_index("TNIndex: {{tnindex}}\n"));
    CHECK_FALSE(mdboss::needs_tn_index("# Meeting Notes\n"));

    CHECK(mdboss::fill_tn_index("TNIndex: {{tnindex}}\n", notes, 2026) ==
          "TNIndex: 2026.07\n");

    // One document, one number -- even where a template names it twice.
    CHECK(mdboss::fill_tn_index("{{tnindex}} and {{tnindex}}", notes, 2026) ==
          "2026.07 and 2026.07");

    // A template with no placeholder comes back untouched, which is what lets
    // the caller skip the scan entirely.
    const std::string plain = "# {{title}}\n";
    CHECK(mdboss::fill_tn_index(plain, notes, 2026) == plain);

    // The number handed out is reported back, so the caller can treat it as
    // taken before the note holding it has been saved.
    std::string assigned = "untouched";
    CHECK(mdboss::fill_tn_index(plain, notes, 2026, &assigned) == plain);
    CHECK(assigned == "untouched");
    mdboss::fill_tn_index("{{tnindex}}", notes, 2026, &assigned);
    CHECK(assigned == "2026.07");
}

TEST_CASE("a bare TNIndex: key is filled too", "[technotes]")
{
    // A hand-maintained template does not carry the placeholder -- it just has
    // the key with nothing after it, which asks the same question.  Supporting
    // it is what makes this work on templates that predate the token.
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"a.md", "A", "g", "", "", "2026.04"});

    const std::string tmpl = "---\ntitle: {{title}}\nGUID: {{guid}}\n"
                             "TNIndex:\nkeywords: TechNote\n---\n\n# x\n";
    CHECK(mdboss::needs_tn_index(tmpl));
    CHECK(mdboss::fill_tn_index(tmpl, notes, 2026).find(
              "\nTNIndex: 2026.05\n") != std::string::npos);

    // CRLF survives: rewriting over the \r would join the line to the next.
    const std::string crlf = "---\r\nTNIndex:\r\nkeywords: TechNote\r\n---\r\n";
    CHECK(mdboss::fill_tn_index(crlf, notes, 2026).find(
              "\r\nTNIndex: 2026.05\r\n") != std::string::npos);

    // A number already chosen is left alone, and so is a nested key.
    const std::string taken = "---\nTNIndex: 2026.01\n---\n";
    CHECK_FALSE(mdboss::needs_tn_index(taken));
    CHECK(mdboss::fill_tn_index(taken, notes, 2026) == taken);
    CHECK_FALSE(mdboss::needs_tn_index("---\nmeta:\n  TNIndex:\n---\n"));

    // Only inside front matter: a TNIndex: line in the body is prose.
    CHECK_FALSE(mdboss::needs_tn_index("# x\n\nTNIndex:\n"));
    CHECK_FALSE(mdboss::needs_tn_index("---\nGUID: g\n---\nTNIndex:\n"));
}

TEST_CASE("promoting a plain document writes a whole block", "[technotes]")
{
    const std::string doc = "# Antenna Notes\n\nSome prose.\n";
    const std::string out =
        mdboss::promote_to_tech_note(doc, "abc-guid", "2026.05", "antenna");

    CHECK(out.rfind("---\n", 0) == 0);
    CHECK(out.find("\ntitle: antenna\n") != std::string::npos);
    CHECK(out.find("\nGUID: abc-guid\n") != std::string::npos);
    CHECK(out.find("\nTNIndex: 2026.05\n") != std::string::npos);
    CHECK(out.find("\nkeywords: TechNote\n") != std::string::npos);
    // The document itself is untouched, below the block.
    CHECK(out.find(doc) != std::string::npos);

    // And it now parses as one, which is the whole point.
    mdboss::TechNote note;
    REQUIRE(mdboss::parse_tech_note(out, &note));
    CHECK(note.tn_index == "2026.05");
}

TEST_CASE("promoting adds only what is missing", "[technotes]")
{
    // A document the user wrote: keys they chose, in the order they chose.
    // The command is "make this a tech note", not "rewrite its front matter".
    const std::string doc = "---\ntitle: Survey\nauthor: B. Graham\n"
                            "keywords: Draft, Radar\n---\n\n# Survey\n";
    const std::string out =
        mdboss::promote_to_tech_note(doc, "g-1", "2026.02", "survey");

    // The existing keywords are extended, NOT replaced, and NOT duplicated
    // onto a second `keywords:` line -- YAML reads that as one key twice.
    CHECK(out.find("keywords: Draft, Radar, TechNote") != std::string::npos);
    CHECK(out.find("keywords: TechNote\n") == std::string::npos);
    CHECK(out.find("\ntitle: Survey\n") != std::string::npos);
    CHECK(out.find("\nauthor: B. Graham\n") != std::string::npos);
    CHECK(out.find("\nGUID: g-1\n") != std::string::npos);
    CHECK(out.find("\nTNIndex: 2026.02\n") != std::string::npos);
    // Still one block, still closed.
    mdboss::TechNote note;
    REQUIRE(mdboss::parse_tech_note(out, &note));
    CHECK(note.title == "Survey");
}

TEST_CASE("promoting never overwrites what is already there", "[technotes]")
{
    // A note that already has a number and a GUID is left exactly alone: it
    // was numbered deliberately, and renumbering it would break a citation.
    const std::string done = "---\nGUID: mine\nTNIndex: 2020.07\n"
                             "keywords: TechNote\n---\n\n# x\n";
    CHECK(mdboss::promote_to_tech_note(done, "new", "2026.09", "x") == done);

    // Half-furnished: only the missing half is added.
    const std::string half = "---\nGUID: mine\nkeywords: Draft\n---\n\n# x\n";
    const std::string out =
        mdboss::promote_to_tech_note(half, "new", "2026.09", "x");
    CHECK(out.find("GUID: mine") != std::string::npos);
    CHECK(out.find("GUID: new") == std::string::npos);
    CHECK(out.find("keywords: Draft, TechNote") != std::string::npos);
}

TEST_CASE("promoting keeps the file's line endings", "[technotes]")
{
    // A CRLF document must not come back with one LF line in its front
    // matter; nothing renders differently, which is what makes it survivable
    // and therefore worth pinning.
    const std::string doc =
        "---\r\ntitle: X\r\nkeywords: Draft\r\n---\r\n\r\n# X\r\n";
    const std::string out =
        mdboss::promote_to_tech_note(doc, "g", "2026.01", "x");
    CHECK(out.find("\r\nGUID: g\r\n") != std::string::npos);
    CHECK(out.find("keywords: Draft, TechNote\r\n") != std::string::npos);
    CHECK(out.find("\nGUID: g\n") == std::string::npos);
}

TEST_CASE("the year comes from the document when it has one", "[technotes]")
{
    // A note written years ago should be numbered in the year it was written.
    CHECK(mdboss::tech_note_year("---\ndate: 2023-04-11\n---\n", 2026) == 2023);
    CHECK(mdboss::tech_note_year("---\ncreated: 2021\n---\n", 2026) == 2021);
    // created wins over updated: the document belongs to the year it started.
    CHECK(mdboss::tech_note_year("---\nupdated: 2025\ncreated: 2019\n---\n",
                                 2026) == 2019);

    // Fallbacks.  A value not led by a year, a year in the future, and one
    // before anything plausible all fall back rather than inventing a year
    // nobody has notes in.
    CHECK(mdboss::tech_note_year("---\ndate: 17 Aug 2026\n---\n", 2026) == 2026);
    CHECK(mdboss::tech_note_year("---\ndate: 2099-01-01\n---\n", 2026) == 2026);
    CHECK(mdboss::tech_note_year("---\ndate: 1234\n---\n", 2026) == 2026);
    CHECK(mdboss::tech_note_year("# no front matter\n", 2026) == 2026);
}

TEST_CASE("the gaps say what a document is missing", "[technotes]")
{
    mdboss::TechNoteGaps gaps = mdboss::tech_note_gaps("# plain\n");
    CHECK(gaps.front_matter);
    CHECK(gaps.guid);
    CHECK(gaps.keyword);
    CHECK(gaps.tn_index);

    gaps = mdboss::tech_note_gaps("---\nGUID: g\nTNIndex: 2026.01\n"
                                  "keywords: TechNote\n---\n");
    CHECK_FALSE(gaps.front_matter);
    CHECK_FALSE(gaps.guid);
    CHECK_FALSE(gaps.keyword);
    CHECK_FALSE(gaps.tn_index);
}

TEST_CASE("the index lists notes and says when it was made", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"C:\\docs\\b.md", "Beta", "g2", "1.0", ""});
    notes.push_back(mdboss::TechNote{"C:\\docs\\a.md", "Alpha", "g1", "0.1",
                                     "Survey", "2026.01"});

    const std::string index =
        mdboss::tech_notes_index(notes, {"C:\\docs"}, "17 Aug 2026");

    CHECK(index.find("# Tech Notes") != std::string::npos);
    // A derived file that does not say when it was derived invites being
    // trusted after it has gone stale.
    CHECK(index.find("17 Aug 2026") != std::string::npos);
    CHECK(index.find("Rebuilt, not edited") != std::string::npos);

    // Paths shown relative to the root that holds them.
    CHECK(index.find("| a.md |") != std::string::npos);
    CHECK(index.find("C:\\docs\\a.md") == std::string::npos);

    CHECK(index.find("2 tech notes") != std::string::npos);

    // The number leads the row: it is what a numbered series is cited by.
    CHECK(index.find("| TN Index | Title |") != std::string::npos);
    // The title is a LINK to the note, so the list is a way in rather than
    // just a report.  Absolute, because the index sits in MD_Internal under
    // one root while the notes it lists may be under any of them.
    CHECK(index.find("| 2026.01 | [Alpha](file:///C:/docs/a.md) |") !=
          std::string::npos);
    // Nothing to report when every number is unique.
    CHECK(index.find("Duplicate numbers") == std::string::npos);
}

TEST_CASE("a title that could break its own link is escaped",
          "[technotes][links]")
{
    std::vector<mdboss::TechNote> notes;
    // A bracket would close the link text early; a space in the path would end
    // the destination and leave the rest of it as visible text.
    notes.push_back(mdboss::TechNote{"C:\\my docs\\a [draft].md",
                                     "Alpha [draft]", "g", "1", "", "2026.01"});
    const std::string index = mdboss::tech_notes_index(notes, {}, "");

    CHECK(index.find("[Alpha \\[draft\\]]") != std::string::npos);
    CHECK(index.find("(file:///C:/my%20docs/a%20[draft].md)") !=
          std::string::npos);
    // Still six separators, so the columns hold.
    CHECK(index.find(" | 1 | ") != std::string::npos);
}

TEST_CASE("a note with no path is not linked", "[technotes][links]")
{
    // tech_notes_index() is a pure function over whatever it is handed, and a
    // link to nowhere is worse than no link.
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"", "Alpha", "g", "1", "", "2026.01"});
    const std::string index = mdboss::tech_notes_index(notes, {}, "");
    CHECK(index.find("| 2026.01 | Alpha |") != std::string::npos);
    CHECK(index.find("file:///") == std::string::npos);
}

TEST_CASE("scanned notes come back in number order", "[technotes]")
{
    // Written to disk because the ordering lives in scan_tech_notes(), which
    // is the one function here that reads files.
    const fs::path dir = fs::temp_directory_path() / "mdboss_tn_order";
    fs::remove_all(dir);
    fs::create_directories(dir);

    write_note(dir / "z.md", "Zulu", "2026.02");
    write_note(dir / "a.md", "Alpha", "2027.01");
    write_note(dir / "m.md", "Mike", "2026.10");
    write_note(dir / "u.md", "Uniform", "");        // no number: sorts last
    write_note(dir / "b.md", "Bravo", "");

    std::vector<std::string> paths;
    for (const char* name : {"z.md", "a.md", "m.md", "u.md", "b.md"}) {
        paths.push_back((dir / name).string());
    }

    const std::vector<mdboss::TechNote> notes = mdboss::scan_tech_notes(paths);
    REQUIRE(notes.size() == 5);
    // 2026.02, then 2026.10 -- NOT 2026.10 before 2026.02, which is what a
    // string comparison of the numbers would give.
    CHECK(notes[0].tn_index == "2026.02");
    CHECK(notes[1].tn_index == "2026.10");
    CHECK(notes[2].tn_index == "2027.01");
    // Unnumbered last, among themselves by title.
    CHECK(notes[3].title == "Bravo");
    CHECK(notes[4].title == "Uniform");

    fs::remove_all(dir);
}

TEST_CASE("a number claimed twice is reported", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"a.md", "A", "g1", "", "", "2026.01"});
    notes.push_back(mdboss::TechNote{"b.md", "B", "g2", "", "", "2026.01"});
    notes.push_back(mdboss::TechNote{"c.md", "C", "g3", "", "", "2026.02"});
    // Two unnumbered notes do not collide with each other.
    notes.push_back(mdboss::TechNote{"d.md", "D", "g4", "", "", ""});
    notes.push_back(mdboss::TechNote{"e.md", "E", "g5", "", "", ""});

    const std::string index = mdboss::tech_notes_index(notes, {}, "");
    CHECK(index.find("Duplicate numbers") != std::string::npos);
    CHECK(index.find("`2026.01`") != std::string::npos);
    CHECK(index.find("`2026.02`") == std::string::npos);
}

TEST_CASE("an empty index says so rather than showing a bare header",
          "[technotes]")
{
    const std::string index = mdboss::tech_notes_index({}, {}, "17 Aug 2026");
    CHECK(index.find("No tech notes found") != std::string::npos);
    // No table header with nothing under it.
    CHECK(index.find("| Title |") == std::string::npos);
}

TEST_CASE("a pipe in a field cannot break the index table", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    notes.push_back(
        mdboss::TechNote{"C:\\docs\\a.md", "Odd | Title", "g", "1", "x|y"});
    const std::string index = mdboss::tech_notes_index(notes, {}, "");
    // Escaped, so the row still has six separators and the columns hold.
    CHECK(index.find("Odd \\| Title") != std::string::npos);
    CHECK(index.find("x\\|y") != std::string::npos);
}

TEST_CASE("one note is singular", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"a.md", "Solo", "g", "", ""});
    const std::string index = mdboss::tech_notes_index(notes, {}, "");
    CHECK(index.find("1 tech note.") != std::string::npos);
    CHECK(index.find("1 tech notes") == std::string::npos);
}
