// Tests for the favorites interchange file and the inbox copy path.
//
// Both are compatibility surfaces with the Python app rather than private
// formats: a list exported from one must import into the other, and an
// imported file must never overwrite one already in the inbox.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Config.h"
#include "Favorites.h"
#include "FileScan.h"
#include "PathUtf8.h"

namespace {

namespace fs = std::filesystem;

}  // namespace

TEST_CASE("export writes the shape Python reads", "[favorites]")
{
    const std::string text =
        mdboss::favorites_to_json({"C:\\notes\\one.md", "C:\\notes\\two.md"});

    // Python's reader does data.get("favorites"), so the wrapping object and
    // that exact key are the contract -- not an implementation detail.
    CHECK(text.find("\"favorites\"") != std::string::npos);
    CHECK(text.find("one.md") != std::string::npos);
    CHECK(text.find("two.md") != std::string::npos);

    // What we write, we must read back.
    const mdboss::FavoritesFile parsed = mdboss::parse_favorites_json(text);
    REQUIRE(parsed.parsed);
    REQUIRE(parsed.paths.size() == 2);
    CHECK(parsed.paths[0] == "C:\\notes\\one.md");
}

TEST_CASE("export is byte-identical to Python's", "[favorites]")
{
    // Taken from the Python app's own serialiser:
    //   json.dumps({"favorites": [...]}, indent=2)
    // Only parse-compatibility is strictly required, but pinning the bytes
    // turns a formatting change in either JSON library into a visible test
    // failure instead of a silent drift between the two apps' exports.
    const std::string expected =
        "{\n"
        "  \"favorites\": [\n"
        "    \"C:\\\\notes\\\\one.md\",\n"
        "    \"C:\\\\notes\\\\two.md\"\n"
        "  ]\n"
        "}";
    CHECK(mdboss::favorites_to_json(
              {"C:\\notes\\one.md", "C:\\notes\\two.md"}) == expected);

    CHECK(mdboss::favorites_to_json({}) == "{\n  \"favorites\": []\n}");
}

TEST_CASE("an empty export is still a favorites file", "[favorites]")
{
    // Not just cosmetic: an unassigned key would serialise as null, which
    // Python rejects as "not a list" -- the file would fail to import at all.
    const std::string text = mdboss::favorites_to_json({});
    const mdboss::FavoritesFile parsed = mdboss::parse_favorites_json(text);
    CHECK(parsed.parsed);
    CHECK(parsed.paths.empty());
}

TEST_CASE("a bare array imports too", "[favorites]")
{
    // Python falls back to the document itself when it is not a dict, so a
    // hand-written list has to work here as well.
    const mdboss::FavoritesFile parsed =
        mdboss::parse_favorites_json("[\"C:\\\\a.md\", \"C:\\\\b.md\"]");
    REQUIRE(parsed.parsed);
    CHECK(parsed.paths.size() == 2);
}

TEST_CASE("junk is refused rather than imported as nothing", "[favorites]")
{
    // "Not a favorites file" and "a favorites file with nothing in it" get
    // different messages, so they must be distinguishable here.
    CHECK_FALSE(mdboss::parse_favorites_json("not json at all").parsed);
    CHECK_FALSE(mdboss::parse_favorites_json("{\"roots\": []}").parsed);
    CHECK_FALSE(mdboss::parse_favorites_json("{\"favorites\": 7}").parsed);

    const mdboss::FavoritesFile empty =
        mdboss::parse_favorites_json("{\"favorites\": []}");
    CHECK(empty.parsed);
    CHECK(empty.paths.empty());
}

TEST_CASE("non-string and blank entries are dropped", "[favorites]")
{
    const mdboss::FavoritesFile parsed = mdboss::parse_favorites_json(
        "{\"favorites\": [\"C:\\\\real.md\", 42, null, \"   \", \"\"]}");
    REQUIRE(parsed.parsed);
    REQUIRE(parsed.paths.size() == 1);
    CHECK(parsed.paths[0] == "C:\\real.md");
}

TEST_CASE("merging keeps the user's order and drops duplicates",
          "[favorites]")
{
    const std::vector<std::string> existing{"C:\\keep.md", "C:\\both.md"};
    const std::vector<std::string> imported{"C:\\BOTH.MD", "C:\\new.md"};

    const std::vector<std::string> merged =
        mdboss::merge_favorites(existing, imported, true, 10);

    REQUIRE(merged.size() == 3);
    CHECK(merged[0] == "C:\\keep.md");
    // The user's own spelling survives; the import does not restyle it.
    CHECK(merged[1] == "C:\\both.md");
    CHECK(merged[2] == "C:\\new.md");
}

TEST_CASE("replacing discards the current list", "[favorites]")
{
    const std::vector<std::string> merged = mdboss::merge_favorites(
        {"C:\\old.md"}, {"C:\\new.md"}, false, 10);
    REQUIRE(merged.size() == 1);
    CHECK(merged[0] == "C:\\new.md");
}

TEST_CASE("a merge cannot grow the list past the cap", "[favorites]")
{
    // The config file holds at most kMaxFavorites; importing twenty must not
    // produce a list the app would silently truncate on the next save.
    std::vector<std::string> many;
    for (int i = 0; i < 20; ++i) {
        many.push_back("C:\\f" + std::to_string(i) + ".md");
    }
    const std::vector<std::string> merged =
        mdboss::merge_favorites({"C:\\mine.md"}, many, true,
                                mdboss::kMaxFavorites);
    CHECK(merged.size() == mdboss::kMaxFavorites);
    CHECK(merged[0] == "C:\\mine.md");
}

// ---- MD_Inbox ------------------------------------------------------------

TEST_CASE("an import never overwrites what is already there", "[inbox]")
{
    const fs::path dir = fs::temp_directory_path() / "mdboss_inbox_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    const std::string folder = mdboss::path_to_utf8(dir);
    // Nothing there yet: the plain name is free.
    CHECK(mdboss::unique_dest(folder, "note.md") ==
          mdboss::path_to_utf8(dir / "note.md"));

    { std::ofstream(dir / "note.md") << "first"; }
    CHECK(mdboss::unique_dest(folder, "note.md") ==
          mdboss::path_to_utf8(dir / "note (2).md"));

    { std::ofstream(dir / "note (2).md") << "second"; }
    CHECK(mdboss::unique_dest(folder, "note.md") ==
          mdboss::path_to_utf8(dir / "note (3).md"));

    // The counter goes before the extension, not after the whole name, or
    // the copy stops being a Markdown file.
    CHECK(mdboss::unique_dest(folder, "note.md").find(".md") !=
          std::string::npos);

    fs::remove_all(dir, ec);
}

TEST_CASE("a name with no extension still gets a counter", "[inbox]")
{
    const fs::path dir = fs::temp_directory_path() / "mdboss_inbox_noext";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    { std::ofstream(dir / "README") << "x"; }

    CHECK(mdboss::unique_dest(mdboss::path_to_utf8(dir), "README") ==
          mdboss::path_to_utf8(dir / "README (2)"));

    fs::remove_all(dir, ec);
}

TEST_CASE("the inbox is found as a root or inside one", "[inbox]")
{
    const fs::path base = fs::temp_directory_path() / "mdboss_inbox_find";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "Plain" / "Sub", ec);
    fs::create_directories(base / "WithInbox" / "MD_Inbox", ec);
    fs::create_directories(base / "md_inbox", ec);
    REQUIRE_FALSE(ec);

    // A root that holds one as a top-level subfolder.
    CHECK(mdboss::norm_path(mdboss::find_inbox(
              {mdboss::path_to_utf8(base / "WithInbox")})) ==
          mdboss::norm_path(
              mdboss::path_to_utf8(base / "WithInbox" / "MD_Inbox")));

    // A root that IS one, matched case-insensitively.
    CHECK(mdboss::norm_path(
              mdboss::find_inbox({mdboss::path_to_utf8(base / "md_inbox")})) ==
          mdboss::norm_path(mdboss::path_to_utf8(base / "md_inbox")));

    // No inbox anywhere: empty, which disables the command rather than
    // guessing at a folder to copy into.
    CHECK(mdboss::find_inbox({mdboss::path_to_utf8(base / "Plain")}).empty());

    // Only top-level subfolders count, so a deeply buried MD_Inbox does not
    // quietly become the target.
    fs::create_directories(base / "Plain" / "Sub" / "MD_Inbox", ec);
    CHECK(mdboss::find_inbox({mdboss::path_to_utf8(base / "Plain")}).empty());

    // First match wins across roots.
    CHECK_FALSE(mdboss::find_inbox({mdboss::path_to_utf8(base / "Plain"),
                                    mdboss::path_to_utf8(base / "md_inbox")})
                    .empty());

    // A root that does not exist is skipped, not fatal.
    CHECK(mdboss::find_inbox({"Z:\\nope", ""}).empty());

    fs::remove_all(base, ec);
}

// ---- Flat-list folders ----------------------------------------------------
//
// In-memory only: Config::save() writes the real %APPDATA% profile, and a
// test has no business touching that.

TEST_CASE("a moved document keeps its place in both lists", "[config][move]")
{
    // Favorites and recents store ABSOLUTE paths, so dragging a file to
    // another folder orphans both unless they are rewritten -- a favourite
    // goes red and a recent stops opening, with nothing saying why.
    mdboss::Config config;
    config.add_favorite("C:\\docs\\a.md");
    config.add_favorite("C:\\docs\\move-me.md");
    config.add_favorite("C:\\docs\\b.md");
    config.push_recent("C:\\docs\\move-me.md");
    config.push_recent("C:\\docs\\other.md");

    const std::size_t favorites_before = config.favorites().size();
    const std::vector<std::string> recents_before = config.recents();

    config.replace_path("C:\\docs\\move-me.md", "C:\\docs\\sub\\move-me.md");

    // Rewritten, not removed-and-re-added: the count is unchanged and the
    // entry is still where it was.  Remove-then-add would promote it to the
    // front, as though it had just been favourited or opened.
    //
    // add_favorite puts NEWEST FIRST, so adding a, move-me, b leaves
    // [b, move-me, a] -- the moved one is in the middle either way, but the
    // neighbours are the reverse of insertion order.
    REQUIRE(config.favorites().size() == favorites_before);
    CHECK(config.favorites()[1] == "C:\\docs\\sub\\move-me.md");
    CHECK(config.favorites()[0] == "C:\\docs\\b.md");
    CHECK(config.favorites()[2] == "C:\\docs\\a.md");

    REQUIRE(config.recents().size() == recents_before.size());
    // push_recent puts newest first, so the moved one is second.
    CHECK(config.recents()[1] == "C:\\docs\\sub\\move-me.md");
    CHECK(config.recents()[0] == recents_before[0]);

    // The old path is gone from both.
    CHECK_FALSE(config.is_favorite("C:\\docs\\move-me.md"));
    CHECK(config.is_favorite("C:\\docs\\sub\\move-me.md"));
}

TEST_CASE("rewriting a path ignores case and separator", "[config][move]")
{
    // The tree reports the path the walk produced; a favourite may have been
    // stored with different case or slashes and is still the same file.
    mdboss::Config config;
    config.add_favorite("C:\\Docs\\Note.md");
    config.replace_path("c:/docs/note.md", "C:\\Docs\\Archive\\Note.md");
    CHECK(config.is_favorite("C:\\Docs\\Archive\\Note.md"));
}

TEST_CASE("rewriting a path nobody stored changes nothing", "[config][move]")
{
    mdboss::Config config;
    config.add_favorite("C:\\docs\\keep.md");
    config.push_recent("C:\\docs\\keep.md");
    config.replace_path("C:\\docs\\absent.md", "C:\\elsewhere\\absent.md");
    CHECK(config.favorites().size() == 1);
    CHECK(config.favorites()[0] == "C:\\docs\\keep.md");
    CHECK(config.recents()[0] == "C:\\docs\\keep.md");
}

TEST_CASE("any folder can be flattened, not just a root", "[config][flat]")
{
    mdboss::Config config;
    // The point of the change: a subfolder is as flattenable as its root, and
    // the two are independent of each other.
    config.set_flat_folder("C:\\docs", true);
    config.set_flat_folder("C:\\docs\\deep\\notes", true);
    CHECK(config.is_flat_folder("C:\\docs"));
    CHECK(config.is_flat_folder("C:\\docs\\deep\\notes"));
    // A folder never flattened is not flat, and neither is one in between.
    CHECK_FALSE(config.is_flat_folder("C:\\docs\\deep"));
    CHECK_FALSE(config.is_flat_folder("C:\\other"));
}

TEST_CASE("flattening is a toggle, and toggling off forgets", "[config][flat]")
{
    mdboss::Config config;
    config.set_flat_folder("C:\\docs", true);
    CHECK(config.is_flat_folder("C:\\docs"));
    config.set_flat_folder("C:\\docs", false);
    CHECK_FALSE(config.is_flat_folder("C:\\docs"));
    // Off on something already off is not an error and adds nothing.
    config.set_flat_folder("C:\\never", false);
    CHECK_FALSE(config.is_flat_folder("C:\\never"));
}

TEST_CASE("flattening twice does not double up the entry", "[config][flat]")
{
    // The stored list is what gets written to config.json; a duplicate would
    // grow it without bound across sessions.
    mdboss::Config config;
    config.set_flat_folder("C:\\docs", true);
    config.set_flat_folder("C:\\docs", true);
    config.set_flat_folder("C:\\docs", true);
    // One "off" must be enough to clear it -- which it is only if one "on"
    // was ever stored.
    config.set_flat_folder("C:\\docs", false);
    CHECK_FALSE(config.is_flat_folder("C:\\docs"));
}

// ---- Expanded folders -----------------------------------------------------

TEST_CASE("the expanded folder list round-trips", "[config][expanded]")
{
    mdboss::Config config;
    // Empty by default: a profile that predates the key reopens collapsed,
    // which is what it did before, rather than expanding something arbitrary.
    CHECK(config.expanded_folders().empty());

    config.set_expanded_folders({"c:\\docs", "c:\\docs\\deep"});
    REQUIRE(config.expanded_folders().size() == 2);
    CHECK(config.expanded_folders()[0] == "c:\\docs");
    CHECK(config.expanded_folders()[1] == "c:\\docs\\deep");

    // Setting replaces rather than accumulates: the panel hands over the whole
    // current state, so anything collapsed must actually disappear.
    config.set_expanded_folders({"c:\\other"});
    REQUIRE(config.expanded_folders().size() == 1);
    CHECK(config.expanded_folders()[0] == "c:\\other");
}

TEST_CASE("the expanded folder list is capped", "[config][expanded]")
{
    // load() truncates at the same number.  If the setter did not, a session
    // could write a file whose tail the next load silently dropped -- saved
    // and restored would disagree with no error anywhere.
    mdboss::Config config;
    std::vector<std::string> many;
    many.reserve(5000);
    for (std::size_t i = 0; i < 5000; ++i) {   // bounded (Rule of 10)
        many.push_back("C:\\f" + std::to_string(i));
    }
    config.set_expanded_folders(many);
    CHECK(config.expanded_folders().size() == 4096);
    // The truncation keeps the front of the list, not a random slice.
    CHECK(config.expanded_folders().front() == "C:\\f0");
}
