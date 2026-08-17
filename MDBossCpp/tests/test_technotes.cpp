// Finding tech notes, and the index built from them.
//
// The detection rule is the part worth pinning: BOTH a GUID and the TechNote
// keyword, so a stray GUID in an unrelated document does not turn it into a
// tech note and removing the keyword deliberately takes one off the list.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TechNotes.h"

namespace {

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

TEST_CASE("the index lists notes and says when it was made", "[technotes]")
{
    std::vector<mdboss::TechNote> notes;
    notes.push_back(mdboss::TechNote{"C:\\docs\\b.md", "Beta", "g2", "1.0", ""});
    notes.push_back(mdboss::TechNote{"C:\\docs\\a.md", "Alpha", "g1", "0.1",
                                     "Survey"});

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
