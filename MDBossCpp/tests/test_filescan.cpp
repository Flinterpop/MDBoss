// Tests for the files-pane scanning rules.
//
// The counting rule is what lets the tree hide folders safely: a folder's
// count is the number of Markdown files anywhere beneath it, so a 0 means
// nothing is being concealed by omitting it.  A folder that could not be read
// is absent from the map entirely, which is deliberately different from 0.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "FileScan.h"

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

TEST_CASE("a missing root yields no counts rather than zeros", "[filescan]")
{
    const auto counts =
        mdboss::md_counts_for_root("Z:\\definitely\\not\\here");
    CHECK(counts.empty());
}
