// Tests for the two search commands' shared matcher.
//
// The matcher is worth pinning because both commands are built on it and
// because its interesting rules are all at the edges: a needle that spans
// lines has to find itself in a document that uses the other line ending, a
// wrap has to be reported rather than silently looking like a list with no
// end, and a scan that could stand still on a zero-length match would hang
// the app rather than merely answer wrongly.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "FileScan.h"
#include "TextSearch.h"

namespace {

namespace fs = std::filesystem;

using mdboss::SearchOptions;

SearchOptions cased()
{
    SearchOptions options;
    options.case_sensitive = true;
    return options;
}

// A throwaway corpus, removed when the fixture goes out of scope.
class TempDocs {
public:
    TempDocs()
    {
        root_ = fs::temp_directory_path() / "mdboss_textsearch_test";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
    }

    ~TempDocs()
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    TempDocs(const TempDocs&) = delete;
    TempDocs& operator=(const TempDocs&) = delete;

    std::string write(const char* name, const std::string& text)
    {
        const fs::path file = root_ / name;
        std::ofstream stream(file, std::ios::binary);
        stream << text;
        stream.close();
        return file.string();
    }

    std::string path(const char* name) const
    {
        return (root_ / name).string();
    }

private:
    fs::path root_;
};

}  // namespace

TEST_CASE("match_length_at compares from one position only")
{
    const std::string text = "alpha beta gamma";
    CHECK(mdboss::match_length_at(text, "beta", 6, SearchOptions{}) == 4);
    CHECK(mdboss::match_length_at(text, "beta", 5, SearchOptions{}) == 0);
    CHECK(mdboss::match_length_at(text, "", 0, SearchOptions{}) == 0);
    // Past the end is a question, not a crash.
    CHECK(mdboss::match_length_at(text, "a", text.size() + 9,
                                  SearchOptions{}) == 0);
    // A needle longer than what is left cannot match.
    CHECK(mdboss::match_length_at(text, "gamma ray", 11, SearchOptions{}) == 0);
}

TEST_CASE("case folding is off by default and honours the option")
{
    const std::string text = "The Radar Site";
    CHECK(mdboss::find_forward(text, "radar", 0, SearchOptions{}).offset == 4);
    CHECK(!mdboss::find_forward(text, "radar", 0, cased()).found());
    CHECK(mdboss::find_forward(text, "Radar", 0, cased()).offset == 4);
}

TEST_CASE("folding cannot corrupt a multi-byte character")
{
    // Two different UTF-8 characters whose bytes are outside 'A'..'Z', so an
    // ASCII fold leaves them distinct -- the property that lets the search
    // work over bytes without decoding.
    const std::string text = "caf\xC3\xA9 note";
    CHECK(mdboss::find_forward(text, "caf\xC3\xA9", 0, SearchOptions{}).offset ==
          0);
    CHECK(!mdboss::find_forward(text, "caf\xC3\x89", 0, SearchOptions{})
               .found());
}

TEST_CASE("a needle spanning lines matches either line ending")
{
    const std::string lf = "one\ntwo\nthree\n";
    const std::string crlf = "one\r\ntwo\r\nthree\r\n";

    // An LF needle finds itself in a CRLF document, and the reported length
    // covers the CR as well -- which is what makes the selection right.
    const mdboss::TextMatch in_crlf =
        mdboss::find_forward(crlf, "one\ntwo", 0, SearchOptions{});
    REQUIRE(in_crlf.found());
    CHECK(in_crlf.offset == 0);
    CHECK(in_crlf.length == 8);   // "one" + CRLF + "two"

    // And the other way round: a block copied out of a CRLF document still
    // finds itself in an LF one.
    const mdboss::TextMatch in_lf =
        mdboss::find_forward(lf, "one\r\ntwo", 0, SearchOptions{});
    REQUIRE(in_lf.found());
    CHECK(in_lf.offset == 0);
    CHECK(in_lf.length == 7);

    // Three lines, to be sure it is not just the first break.
    CHECK(mdboss::find_forward(crlf, "one\ntwo\nthree", 0, SearchOptions{})
              .found());
}

TEST_CASE("a needle of nothing but carriage returns is not a match")
{
    // It consumes no text, so treating it as a match would let hits_in_text
    // stand still on it.
    CHECK(!mdboss::find_forward("abc", "\r", 0, SearchOptions{}).found());
    CHECK(mdboss::hits_in_text("abc", "\r", SearchOptions{}, 8).empty());
}

TEST_CASE("find_forward and find_backward do not wrap")
{
    const std::string text = "aa bb aa bb aa";
    CHECK(mdboss::find_forward(text, "aa", 1, SearchOptions{}).offset == 6);
    CHECK(!mdboss::find_forward(text, "aa", 13, SearchOptions{}).found());
    CHECK(mdboss::find_backward(text, "aa", 12, SearchOptions{}).offset == 6);
    CHECK(!mdboss::find_backward(text, "aa", 0, SearchOptions{}).found());
}

TEST_CASE("find_next and find_previous wrap, and say that they did")
{
    const std::string text = "aa bb aa";

    const mdboss::TextMatch forward =
        mdboss::find_next(text, "aa", 7, SearchOptions{});
    REQUIRE(forward.found());
    CHECK(forward.offset == 0);
    CHECK(forward.wrapped);

    // Not a wrap when there was somewhere left to go.
    const mdboss::TextMatch onwards =
        mdboss::find_next(text, "aa", 1, SearchOptions{});
    CHECK(onwards.offset == 6);
    CHECK(!onwards.wrapped);

    const mdboss::TextMatch backward =
        mdboss::find_previous(text, "aa", 0, SearchOptions{});
    REQUIRE(backward.found());
    CHECK(backward.offset == 6);
    CHECK(backward.wrapped);

    // The single match in a document, found again where it already is, has
    // not wrapped -- reporting it as such is the easy mistake.
    const mdboss::TextMatch only =
        mdboss::find_previous("xx yy", "yy", 5, SearchOptions{});
    REQUIRE(only.found());
    CHECK(only.offset == 3);
    CHECK(!only.wrapped);
}

TEST_CASE("line numbers and excerpts describe the line the match is on")
{
    const std::string text = "first\n   second line   \nthird\n";
    CHECK(mdboss::line_number_at(text, 0) == 1);
    CHECK(mdboss::line_number_at(text, 9) == 2);
    CHECK(mdboss::line_number_at(text, text.size() + 99) == 4);
    CHECK(mdboss::excerpt_at(text, 9) == "second line");
    // A CR belongs to the line ending, not to the text of the line.
    CHECK(mdboss::excerpt_at("a\r\nbee\r\n", 3) == "bee");
    CHECK(mdboss::excerpt_at("", 0).empty());

    const std::string wide(400, 'x');
    const std::string excerpt = mdboss::excerpt_at(wide, 10);
    CHECK(excerpt.size() == 163);
    CHECK(excerpt.substr(160) == "...");
}

TEST_CASE("hits_in_text reports every match, without overlaps")
{
    const std::string text = "aaaa\naa\n";
    const std::vector<mdboss::LineHit> hits =
        mdboss::hits_in_text(text, "aa", SearchOptions{}, 100);
    REQUIRE(hits.size() == 3);
    CHECK(hits[0].offset == 0);
    CHECK(hits[1].offset == 2);
    CHECK(hits[2].offset == 5);
    CHECK(hits[0].line == 1);
    CHECK(hits[2].line == 2);

    // The cap is a cap, not a suggestion.
    CHECK(mdboss::hits_in_text(text, "aa", SearchOptions{}, 2).size() == 2);
    CHECK(mdboss::hits_in_text(text, "aa", SearchOptions{}, 0).empty());
}

TEST_CASE("hits_in_text numbers lines the same as counting from the start")
{
    // The incremental counter is the optimisation most likely to be wrong, so
    // it is checked against the obvious implementation.
    const std::string text =
        "x\ny\nfind\n\n\nfind\r\nz\nfind\nlast line find\n";
    const std::vector<mdboss::LineHit> hits =
        mdboss::hits_in_text(text, "find", SearchOptions{}, 100);
    REQUIRE(hits.size() == 4);
    for (const mdboss::LineHit& hit : hits) {
        CHECK(hit.line == mdboss::line_number_at(text, hit.offset));
    }
    CHECK(hits.back().line == 9);
}

TEST_CASE("search_documents finds every match in the documents it is given")
{
    TempDocs docs;
    const std::string one =
        docs.write("one.md", "# Radar\nthe radar site\nnothing\nRADAR again\n");
    const std::string two = docs.write("two.md", "no mention here\n");
    const std::string three = docs.write("three.md", "radar\n");

    const mdboss::DocumentSearch result =
        mdboss::search_documents({one, two, three}, "radar");
    CHECK(result.documents_searched == 3);
    CHECK(result.documents_matched == 2);
    CHECK(!result.truncated);
    REQUIRE(result.matches.size() == 4);
    // In the order the documents were given, and by position within each.
    CHECK(result.matches[0].path == one);
    CHECK(result.matches[0].line == 1);
    CHECK(result.matches[1].line == 2);
    CHECK(result.matches[1].text == "the radar site");
    CHECK(result.matches[2].line == 4);
    CHECK(result.matches[3].path == three);
    // The offset is what lets a result select the match rather than the line.
    CHECK(result.matches[1].offset == 12);
}

TEST_CASE("search_documents honours case sensitivity")
{
    TempDocs docs;
    const std::string file = docs.write("c.md", "Radar\nradar\n");
    CHECK(mdboss::search_documents({file}, "radar").matches.size() == 2);
    CHECK(mdboss::search_documents({file}, "radar", cased()).matches.size() ==
          1);
}

TEST_CASE("search_documents finds a block that spans lines")
{
    TempDocs docs;
    const std::string lf = docs.write("lf.md", "intro\nalpha\nbeta\nend\n");
    const std::string crlf =
        docs.write("crlf.md", "intro\r\nalpha\r\nbeta\r\nend\r\n");

    // One needle, both line endings: the block was copied out of one document
    // and looked for in every other, which is the whole point of the command.
    const mdboss::DocumentSearch result =
        mdboss::search_documents({lf, crlf}, "alpha\nbeta");
    CHECK(result.documents_matched == 2);
    REQUIRE(result.matches.size() == 2);
    CHECK(result.matches[0].line == 2);
    CHECK(result.matches[1].line == 2);
}

TEST_CASE("search_documents refuses a needle too short to be useful")
{
    TempDocs docs;
    const std::string file = docs.write("s.md", "aaaa\n");
    CHECK(mdboss::search_documents({file}, "a").matches.empty());
    CHECK(mdboss::search_documents({file}, "").matches.empty());
}

TEST_CASE("search_documents caps one document's hits and says so")
{
    TempDocs docs;
    std::string body;
    for (std::size_t i = 0; i < mdboss::kMaxHitsPerDocument + 20; ++i) {
        body += "hit\n";
    }
    const std::string file = docs.write("many.md", body);

    const mdboss::DocumentSearch result =
        mdboss::search_documents({file}, "hit");
    CHECK(result.matches.size() == mdboss::kMaxHitsPerDocument);
    // A bound that can be hit has to be reportable; a capped list presented
    // as the whole answer is worse than a slow one.
    CHECK(result.truncated);
}

TEST_CASE("search_documents survives a path that is not there")
{
    TempDocs docs;
    const std::string real = docs.write("real.md", "found me\n");
    const mdboss::DocumentSearch result =
        mdboss::search_documents({docs.path("gone.md"), real}, "found");
    CHECK(result.documents_searched == 1);
    REQUIRE(result.matches.size() == 1);
    CHECK(result.matches[0].path == real);
}

TEST_CASE("search_documents stops when asked, and reports the answer partial")
{
    TempDocs docs;
    const std::string one = docs.write("a.md", "stop here\n");
    const std::string two = docs.write("b.md", "stop here too\n");

    const mdboss::DocumentSearch result =
        mdboss::search_documents({one, two}, "stop", SearchOptions{},
                                 [] { return true; });
    CHECK(result.matches.empty());
    CHECK(result.truncated);
}
