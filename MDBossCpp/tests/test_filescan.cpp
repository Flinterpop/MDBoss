// Tests for the files-pane scanning rules.
//
// The counting rule is what lets the tree hide folders safely: a folder's
// count is the number of Markdown files anywhere beneath it, so a 0 means
// nothing is being concealed by omitting it.  A folder that could not be read
// is absent from the map entirely, which is deliberately different from 0.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "FileScan.h"
#include "PathUtf8.h"

namespace {

namespace fs = std::filesystem;

// A throwaway tree, removed when the fixture goes out of scope.
class TempTree {
public:
    TempTree()
    {
        root_ = fs::temp_directory_path() / "mdboss_filescan_test";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "Notes" / "Sub", ec);
        fs::create_directories(root_ / "Empty" / "Deeper", ec);
        write("top.md");
        write("Notes/alpha.md");
        write("Notes/beta.md");
        write("Notes/Sub/gamma.md");
        write("Notes/notes.txt");
        write("Empty/readme.txt");
        write("Empty/Deeper/more.txt");
    }

    ~TempTree()
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    std::string path() const { return root_.string(); }
    std::string sub(const char* rel) const { return (root_ / rel).string(); }

private:
    void write(const char* rel)
    {
        std::ofstream stream(root_ / rel, std::ios::binary);
        stream << "# x\n";
    }

    fs::path root_;
};

}  // namespace

TEST_CASE("is_markdown accepts the documented extensions", "[filescan]")
{
    CHECK(mdboss::is_markdown("a.md"));
    CHECK(mdboss::is_markdown("a.MD"));
    CHECK(mdboss::is_markdown("a.markdown"));
    CHECK(mdboss::is_markdown("a.mdown"));
    CHECK(mdboss::is_markdown("a.mkd"));
    CHECK(mdboss::is_markdown("a.mdwn"));
    CHECK_FALSE(mdboss::is_markdown("a.txt"));
    CHECK_FALSE(mdboss::is_markdown("a.mdx"));
    CHECK_FALSE(mdboss::is_markdown("noextension"));
    CHECK_FALSE(mdboss::is_markdown(""));
}

TEST_CASE("markdown counts are recursive", "[filescan]")
{
    const TempTree tree;
    const auto counts = mdboss::md_counts_for_root(tree.path());

    const auto at = [&counts](const std::string& path) {
        const auto found = counts.find(mdboss::norm_path(path));
        REQUIRE(found != counts.end());
        return found->second;
    };

    // 4 Markdown files in the whole tree; the .txt files never count.
    CHECK(at(tree.path()) == 4);
    CHECK(at(tree.sub("Notes")) == 3);       // alpha, beta, and Sub/gamma
    CHECK(at(tree.sub("Notes/Sub")) == 1);

    // The rule the tree relies on: a folder with no Markdown anywhere beneath
    // it counts 0, at every level, so hiding it conceals nothing.
    CHECK(at(tree.sub("Empty")) == 0);
    CHECK(at(tree.sub("Empty/Deeper")) == 0);
}

TEST_CASE("directory listing is ordered and filtered", "[filescan]")
{
    const TempTree tree;
    const auto entries = mdboss::list_directory(tree.sub("Notes"));

    // Directories first, then Markdown files by name; notes.txt is dropped.
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].name == "Sub");
    CHECK(entries[0].is_dir);
    CHECK(entries[1].name == "alpha.md");
    CHECK(entries[2].name == "beta.md");
    for (std::size_t i = 1; i < entries.size(); ++i) {
        CHECK_FALSE(entries[i].is_dir);
    }
}

TEST_CASE("delete goes to the Recycle Bin, recoverably", "[filescan]")
{
    // Deleting is destructive and the flag that makes it recoverable
    // (FOF_ALLOWUNDO) is easy to get wrong silently, so exercise it for real.
    //
    // This deliberately leaves one small file in the Recycle Bin per run --
    // that residue IS the proof the delete was recoverable rather than an
    // unlink.  Verified once by hand: the item appears in the bin with its
    // original location recorded.
    const fs::path file =
        fs::temp_directory_path() / "mdboss_recycle_probe.md";
    {
        std::ofstream stream(file, std::ios::binary);
        stream << "# recycle me\n";
    }
    REQUIRE(fs::exists(file));

    CHECK(mdboss::send_to_recycle_bin(file.string()));
    CHECK_FALSE(fs::exists(file));
}

TEST_CASE("deleting nothing is refused rather than guessed at", "[filescan]")
{
    CHECK_FALSE(mdboss::send_to_recycle_bin(""));
    CHECK_FALSE(mdboss::send_to_recycle_bin(
        (fs::temp_directory_path() / "mdboss_not_here_at_all.md").string()));
}

// Hidden ([.]) so it never runs in a normal ctest sweep: it scans a real
// folder named by MDBOSS_SCAN_PATH.  Useful for answering "is the scanner
// slow, or is it returning nothing?" about an actual tree, which is exactly
// the question a screenshot of a tree full of (0) cannot answer.
//
//   mdrender_tests.exe "[scanprobe]"
TEST_CASE("scan a real folder", "[.][scanprobe]")
{
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "MDBOSS_SCAN_PATH") != 0 || value == nullptr) {
        WARN("set MDBOSS_SCAN_PATH to a folder to use this probe");
        return;
    }
    const std::string root(value);
    std::free(value);

    std::error_code exists_ec;
    std::error_code dir_ec;
    const bool exists = fs::exists(fs::path(root), exists_ec);
    const bool is_dir = fs::is_directory(fs::path(root), dir_ec);
    WARN("exists=" << exists << " (" << exists_ec.message() << ") is_dir="
                   << is_dir << " (" << dir_ec.message() << ")");

    const auto start = std::chrono::steady_clock::now();
    const auto counts = mdboss::md_counts_for_root(root);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    const auto at_root = counts.find(mdboss::norm_path(root));
    WARN("scanned " << root << " in " << elapsed.count() << " ms: "
                    << counts.size() << " folders, root count "
                    << (at_root == counts.end() ? -1 : at_root->second));
    CHECK_FALSE(counts.empty());
}

TEST_CASE("a missing root yields no counts rather than zeros", "[filescan]")
{
    const auto counts =
        mdboss::md_counts_for_root("Z:\\definitely\\not\\here");
    CHECK(counts.empty());
}

TEST_CASE("a byte-order mark is stripped from content", "[filescan]")
{
    // The failure this prevents is quiet and confusing: with the mark still
    // in front of it, "# Heading" is no longer a heading and the document
    // renders as plain text.
    const std::string bom = "\xEF\xBB\xBF";
    CHECK(mdboss::strip_utf8_bom(bom + "# Heading\n") == "# Heading\n");

    // Only at the start, and only once: a mark in the middle of a document
    // is a zero-width no-break space the author put there.
    CHECK(mdboss::strip_utf8_bom(bom + bom + "x") == bom + "x");
    CHECK(mdboss::strip_utf8_bom("a" + bom + "b") == "a" + bom + "b");

    // Files without one are the common case and must be untouched.
    CHECK(mdboss::strip_utf8_bom("# Heading\n") == "# Heading\n");
    CHECK(mdboss::strip_utf8_bom("").empty());
    // Short inputs must not be mistaken for a truncated mark.
    CHECK(mdboss::strip_utf8_bom("\xEF\xBB") == "\xEF\xBB");
}

TEST_CASE("a new document keeps the extension you typed", "[filescan]")
{
    // Bare names become Markdown, which is the common case.
    CHECK(mdboss::ensure_markdown_extension("untitled") == "untitled.md");
    CHECK(mdboss::ensure_markdown_extension("notes") == "notes.md");

    // An extension the user typed is left alone -- including one that is not
    // Markdown.  Appending unconditionally would give "notes.txt.md".
    CHECK(mdboss::ensure_markdown_extension("notes.txt") == "notes.txt");
    CHECK(mdboss::ensure_markdown_extension("readme.md") == "readme.md");
    CHECK(mdboss::ensure_markdown_extension("page.markdown") ==
          "page.markdown");

    // A dotted name is not an extensionless one: "v1.2 notes" ends in
    // ".2 notes", so nothing is appended -- same as Python's splitext.
    CHECK(mdboss::ensure_markdown_extension("v1.2 notes") == "v1.2 notes");
}

// ---- File stamps ---------------------------------------------------------
//
// These decide whether the document watcher reloads.  Both mistakes are bad
// and neither is loud: too eager and the app reloads on the echo of its own
// save, wiping the caret out from under the user for no reason; too slack and
// an outside edit is never noticed, which is the whole feature failing.

namespace {

std::string write_probe(const std::string& name, const std::string& body)
{
    const fs::path file = fs::temp_directory_path() / name;
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream << body;
    stream.close();
    return mdboss::path_to_utf8(file);
}

}  // namespace

TEST_CASE("a stamp is stable when nothing changes", "[filescan][stamp]")
{
    const std::string file =
        write_probe("mdboss_stamp_stable.md", "# One\n");
    const mdboss::FileStamp first = mdboss::stamp_of(file);
    CHECK(first.exists);
    // Re-stamping an untouched file must compare equal, or every filesystem
    // event would look like a change and the app would reload constantly.
    CHECK(first == mdboss::stamp_of(file));

    std::error_code ec;
    fs::remove(fs::path(mdboss::path_from_utf8(file)), ec);
}

TEST_CASE("a stamp notices a same-length edit", "[filescan][stamp]")
{
    // The case that a size comparison alone would miss, and the reason the
    // stamp carries the modification time at tick resolution: a one-character
    // correction leaves the file exactly as long as it was.
    const std::string file = write_probe("mdboss_stamp_samelen.md", "# One\n");
    const mdboss::FileStamp before = mdboss::stamp_of(file);

    // Windows advances the file-time clock in ~15.6 ms steps, so two writes
    // in immediate succession can genuinely carry the same timestamp.  The
    // pause makes the test deterministic rather than papering over that; in
    // the app the settle delay is 300 ms, comfortably clear of the step.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    const std::string again = write_probe("mdboss_stamp_samelen.md", "# Two\n");
    CHECK(again == file);
    const mdboss::FileStamp after = mdboss::stamp_of(file);

    CHECK(before.size == after.size);
    CHECK(before != after);

    std::error_code ec;
    fs::remove(fs::path(mdboss::path_from_utf8(file)), ec);
}

TEST_CASE("a stamp notices a length change", "[filescan][stamp]")
{
    const std::string file = write_probe("mdboss_stamp_grew.md", "# One\n");
    const mdboss::FileStamp before = mdboss::stamp_of(file);
    write_probe("mdboss_stamp_grew.md", "# One\n\nAnd a second paragraph.\n");
    const mdboss::FileStamp after = mdboss::stamp_of(file);

    CHECK(before != after);
    CHECK(after.size > before.size);

    std::error_code ec;
    fs::remove(fs::path(mdboss::path_from_utf8(file)), ec);
}

TEST_CASE("a missing file stamps as absent, not as empty", "[filescan][stamp]")
{
    const std::string missing = mdboss::path_to_utf8(
        fs::temp_directory_path() / "mdboss_stamp_absent.md");
    std::error_code ec;
    fs::remove(fs::path(mdboss::path_from_utf8(missing)), ec);

    const mdboss::FileStamp gone = mdboss::stamp_of(missing);
    CHECK_FALSE(gone.exists);
    // Two absent stamps are equal, so a file that stays deleted is reported
    // once rather than on every notification the folder happens to raise.
    CHECK(gone == mdboss::stamp_of(missing));
    // And absent must never equal present, whatever the other fields hold --
    // that is what makes "the file came back" a change worth reloading for.
    const std::string real = write_probe("mdboss_stamp_absent.md", "x");
    CHECK(gone != mdboss::stamp_of(real));
    fs::remove(fs::path(mdboss::path_from_utf8(real)), ec);
}

TEST_CASE("a directory does not stamp as a file", "[filescan][stamp]")
{
    // The watcher stamps whatever path it was given; a folder where a file
    // was expected must read as absent rather than as a zero-length file.
    const std::string dir = mdboss::path_to_utf8(fs::temp_directory_path());
    CHECK_FALSE(mdboss::stamp_of(dir).exists);
}

// ---- Suggested filename from a document title ----------------------------

TEST_CASE("an ordinary title becomes an ordinary filename", "[filescan][name]")
{
    CHECK(mdboss::filename_from_title("The Rule") == "The Rule.md");
    CHECK(mdboss::filename_from_title("  padded  ") == "padded.md");
    // Inner runs of whitespace collapse to one space.
    CHECK(mdboss::filename_from_title("a\t \tb") == "a b.md");
}

TEST_CASE("characters Windows forbids are removed", "[filescan][name]")
{
    // Each becomes a space rather than vanishing, so words do not run together.
    CHECK(mdboss::filename_from_title("Parse: the rule") == "Parse the rule.md");
    CHECK(mdboss::filename_from_title("a/b\\c") == "a b c.md");
    CHECK(mdboss::filename_from_title("what? yes! <ok>") == "what yes! ok.md");
    CHECK(mdboss::filename_from_title(std::string("nul\x01here")) ==
          "nul here.md");
}

TEST_CASE("markdown markers are dropped from the name", "[filescan][name]")
{
    CHECK(mdboss::filename_from_title("The `parse()` **rule**") ==
          "The parse() rule.md");
    CHECK(mdboss::filename_from_title("_Emphasis_") == "Emphasis.md");
}

TEST_CASE("a title with nothing usable offers nothing", "[filescan][name]")
{
    // Empty beats invented: the Save dialog simply opens with an empty name.
    CHECK(mdboss::filename_from_title("") == "");
    CHECK(mdboss::filename_from_title("   ") == "");
    CHECK(mdboss::filename_from_title("***") == "");
    CHECK(mdboss::filename_from_title("///") == "");
}

TEST_CASE("trailing dots and spaces are stripped", "[filescan][name]")
{
    // Windows strips these itself, leaving a file whose real name differs
    // from the one the user was shown.
    CHECK(mdboss::filename_from_title("Version 1.") == "Version 1.md");
    CHECK(mdboss::filename_from_title("Trailing   ") == "Trailing.md");
    CHECK(mdboss::filename_from_title("...") == "");
}

TEST_CASE("reserved device names are refused", "[filescan][name]")
{
    // "CON.md" cannot be created on Windows, whatever the extension.
    CHECK(mdboss::filename_from_title("CON") == "");
    CHECK(mdboss::filename_from_title("con") == "");
    CHECK(mdboss::filename_from_title("LPT9") == "");
    // ...but a name that merely starts with one is fine.
    CHECK(mdboss::filename_from_title("CONTENTS") == "CONTENTS.md");
    CHECK(mdboss::filename_from_title("Console") == "Console.md");
}

TEST_CASE("an overlong title is capped", "[filescan][name]")
{
    const std::string long_title(400, 'x');
    const std::string name = mdboss::filename_from_title(long_title);
    // Capped, still valid, and still ends in .md.
    CHECK(name.size() <= 103);
    CHECK(name.size() > 10);
    CHECK(name.substr(name.size() - 3) == ".md");
}

// ---- Searching file contents ---------------------------------------------

namespace {

// A small tree to search: two levels, five documents, one of them not
// Markdown so the walk's own filtering is exercised too.
fs::path make_search_tree()
{
    const fs::path base = fs::temp_directory_path() / "mdboss_search";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "deep" / "deeper", ec);

    auto put = [](const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    };
    put(base / "alpha.md", "# Alpha\n\nthe quick brown fox\nsecond line\n");
    put(base / "beta.md", "# Beta\n\nnothing of interest here\n");
    put(base / "deep" / "gamma.md", "# Gamma\n\nline one\nline two\nQUICK "
                                    "shout\n");
    put(base / "deep" / "deeper" / "delta.md", "# Delta\n\n   padded quick  \n");
    put(base / "notes.txt", "quick but not markdown\n");
    return base;
}

std::vector<std::string> names_of(const std::vector<mdboss::ContentMatch>& m)
{
    std::vector<std::string> out;
    for (const auto& hit : m) {
        out.push_back(mdboss::path_to_utf8(
            mdboss::path_from_utf8(hit.path).filename()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("content search finds files at any depth", "[filescan][search]")
{
    const fs::path base = make_search_tree();
    const auto hits =
        mdboss::search_file_contents(mdboss::path_to_utf8(base), "quick");
    // alpha (top), gamma (one down), delta (two down) -- and NOT beta, which
    // does not contain it, nor notes.txt, which is not Markdown.
    CHECK(names_of(hits) ==
          std::vector<std::string>{"alpha.md", "delta.md", "gamma.md"});
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("content search is case-insensitive", "[filescan][search]")
{
    const fs::path base = make_search_tree();
    // gamma.md holds "QUICK" in caps; searching lowercase must still find it,
    // and searching caps must find the lowercase ones.
    CHECK(names_of(mdboss::search_file_contents(mdboss::path_to_utf8(base),
                                                "QUICK"))
              .size() == 3);
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("a match reports its line number and text", "[filescan][search]")
{
    const fs::path base = make_search_tree();
    const auto hits =
        mdboss::search_file_contents(mdboss::path_to_utf8(base), "brown");
    REQUIRE(hits.size() == 1);
    // "# Alpha" is line 1, blank is 2, the text is line 3.
    CHECK(hits.front().line == 3);
    CHECK(hits.front().text == "the quick brown fox");
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("the reported line is trimmed", "[filescan][search]")
{
    // delta.md's line is "   padded quick  " -- shown without the padding, or
    // the tree fills with ragged whitespace.
    const fs::path base = make_search_tree();
    const auto hits =
        mdboss::search_file_contents(mdboss::path_to_utf8(base), "padded");
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().text == "padded quick");
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("a too-short needle searches nothing", "[filescan][search]")
{
    // Guard against reading every file to match one letter, which returns
    // essentially everything and costs the most to find out.
    const fs::path base = make_search_tree();
    CHECK(mdboss::search_file_contents(mdboss::path_to_utf8(base), "q").empty());
    CHECK(mdboss::search_file_contents(mdboss::path_to_utf8(base), "").empty());
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("a stop request abandons the search", "[filescan][search]")
{
    // The panel cancels a search the moment the query moves on; a search that
    // ignored that would keep a worker reading files nobody is waiting for.
    const fs::path base = make_search_tree();
    const auto hits = mdboss::search_file_contents(
        mdboss::path_to_utf8(base), "quick", [] { return true; });
    CHECK(hits.empty());
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("searching a missing root yields nothing, not a throw",
          "[filescan][search]")
{
    CHECK(mdboss::search_file_contents("Z:\\no\\such\\place", "quick").empty());
    CHECK(mdboss::search_file_contents("", "quick").empty());
}

// ---- Markdown image references -------------------------------------------

TEST_CASE("an image beside the document is referenced relatively",
          "[filescan][image]")
{
    // The portable case, and the common one: the pair moves or commits
    // together and the reference keeps working.
    CHECK(mdboss::markdown_image_link("C:\\docs\\shot.png",
                                      "C:\\docs\\note.md") ==
          "![shot](shot.png)");
    CHECK(mdboss::markdown_image_link("C:\\docs\\img\\shot.png",
                                      "C:\\docs\\note.md") ==
          "![shot](img/shot.png)");
}

TEST_CASE("separators come out as forward slashes", "[filescan][image]")
{
    // A Markdown destination is a URL, where a backslash is an escape: a
    // Windows path pasted in raw arrives at the renderer with no separators.
    const std::string link = mdboss::markdown_image_link(
        "C:\\docs\\a\\b\\shot.png", "C:\\docs\\note.md");
    CHECK(link == "![shot](a/b/shot.png)");
    CHECK(link.find('\\') == std::string::npos);
}

TEST_CASE("a path needing escape is wrapped in angle brackets",
          "[filescan][image]")
{
    // CommonMark's own form for a destination containing spaces, and readable
    // in a way percent-encoding is not.
    CHECK(mdboss::markdown_image_link("C:\\docs\\my shot.png",
                                      "C:\\docs\\note.md") ==
          "![my shot](<my shot.png>)");
    CHECK(mdboss::markdown_image_link("C:\\docs\\shot (2).png",
                                      "C:\\docs\\note.md") ==
          "![shot (2)](<shot (2).png>)");
}

TEST_CASE("an unsaved document gets an absolute path", "[filescan][image]")
{
    // There is no folder to be relative to yet, so the only thing that can
    // resolve is the full path.
    const std::string link =
        mdboss::markdown_image_link("C:\\pics\\shot.png", "");
    CHECK(link == "![shot](C:/pics/shot.png)");
}

TEST_CASE("a different drive gets an absolute path", "[filescan][image]")
{
    // No relative path exists between drives; inventing one would produce a
    // reference that silently resolves nowhere.
    const std::string link =
        mdboss::markdown_image_link("D:\\pics\\shot.png", "C:\\docs\\note.md");
    CHECK(link == "![shot](D:/pics/shot.png)");
}

TEST_CASE("a parent-folder image still goes relative", "[filescan][image]")
{
    // docs/note.md referring to images/ beside docs/ is a normal layout, and
    // "../" keeps working when the whole tree moves.
    CHECK(mdboss::markdown_image_link("C:\\proj\\images\\shot.png",
                                      "C:\\proj\\docs\\note.md") ==
          "![shot](../images/shot.png)");
}

// ---- Is a document inside the configured tree? ---------------------------
//
// The title bar shows an out-of-tree document's full path, so a wrong answer
// here either hides where a stray file came from or clutters the title of
// every ordinary one.

TEST_CASE("a document under a root is inside the tree", "[filescan][roots]")
{
    const std::vector<std::string> roots{"C:\\Docs", "D:\\Notes"};
    CHECK(mdboss::is_under_any_root("C:\\Docs\\a.md", roots));
    CHECK(mdboss::is_under_any_root("C:\\Docs\\deep\\nested\\a.md", roots));
    CHECK(mdboss::is_under_any_root("D:\\Notes\\a.md", roots));
    // Case and separator spelling must not decide it: Windows paths reach the
    // app from a drop, a command line and the registry, each with its own.
    CHECK(mdboss::is_under_any_root("c:\\docs\\A.MD", roots));
    CHECK(mdboss::is_under_any_root("C:/Docs/a.md", roots));
    // A root spelled with a trailing separator is the same root.
    CHECK(mdboss::is_under_any_root("C:\\Docs\\a.md", {"C:\\Docs\\"}));
}

TEST_CASE("a sibling folder is not inside the tree", "[filescan][roots]")
{
    const std::vector<std::string> roots{"C:\\Docs"};
    // The bug a bare prefix test would have: "C:\Docs2" starts with "C:\Docs".
    CHECK_FALSE(mdboss::is_under_any_root("C:\\Docs2\\a.md", roots));
    CHECK_FALSE(mdboss::is_under_any_root("C:\\DocsOther\\a.md", roots));
    CHECK_FALSE(mdboss::is_under_any_root("C:\\Elsewhere\\a.md", roots));
    CHECK_FALSE(mdboss::is_under_any_root("D:\\Docs\\a.md", roots));
}

TEST_CASE("the degenerate cases do not report inside", "[filescan][roots]")
{
    // No roots configured means no tree, so nothing can be in it.
    CHECK_FALSE(mdboss::is_under_any_root("C:\\Docs\\a.md", {}));
    CHECK_FALSE(mdboss::is_under_any_root("", {"C:\\Docs"}));
    // An empty root would otherwise swallow every path.
    CHECK_FALSE(mdboss::is_under_any_root("C:\\Docs\\a.md", {""}));
    // The root itself is a folder, not a document in the tree.
    CHECK_FALSE(mdboss::is_under_any_root("C:\\Docs", {"C:\\Docs"}));
}
