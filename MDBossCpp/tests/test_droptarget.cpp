// Which of several dropped files gets opened.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "FileScan.h"

TEST_CASE("a single dropped file is chosen", "[drop]")
{
    CHECK(mdboss::choose_dropped_file({"C:\\docs\\notes.md"}) ==
          "C:\\docs\\notes.md");
}

TEST_CASE("Markdown wins over whatever came first", "[drop]")
{
    // Dropping a multi-selection should open the document, not the image
    // that happened to sort first.
    const std::vector<std::string> files = {
        "C:\\docs\\picture.png",
        "C:\\docs\\archive.zip",
        "C:\\docs\\notes.md",
    };
    CHECK(mdboss::choose_dropped_file(files) == "C:\\docs\\notes.md");
}

TEST_CASE("the first Markdown file wins, not the last", "[drop]")
{
    const std::vector<std::string> files = {
        "C:\\a.png", "C:\\first.md", "C:\\second.md",
    };
    CHECK(mdboss::choose_dropped_file(files) == "C:\\first.md");
}

TEST_CASE("a non-Markdown drop still offers something", "[drop]")
{
    // The app asks before opening a non-Markdown file, so handing it over is
    // better than a drop that silently does nothing.
    CHECK(mdboss::choose_dropped_file({"C:\\docs\\readme.txt"}) ==
          "C:\\docs\\readme.txt");
}

TEST_CASE("an empty drop chooses nothing", "[drop]")
{
    CHECK(mdboss::choose_dropped_file({}).empty());
}

TEST_CASE("extension matching looks at the leaf, not the path", "[drop]")
{
    // A directory called "notes.md" further up the path must not make a .txt
    // file look like Markdown.
    CHECK(mdboss::choose_dropped_file({"C:\\notes.md\\readme.txt"}) ==
          "C:\\notes.md\\readme.txt");
    CHECK(mdboss::choose_dropped_file(
              {"C:\\notes.md\\readme.txt", "C:\\x\\real.md"}) ==
          "C:\\x\\real.md");
}
