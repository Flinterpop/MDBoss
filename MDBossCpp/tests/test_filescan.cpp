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
