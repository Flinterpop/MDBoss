// Tests for the encoding validators and the checked file writer.
//
// These exist because v1.2.0 shipped a save path that could write a text
// buffer whose first 16 bytes the heap had already reclaimed: six documents
// on this machine had their heads replaced by two freed-block pointers
// (NULs included), and the strict FromUTF8 load path then presented such a
// file as an EMPTY editor -- one Ctrl+S from wiping it entirely.  The
// validator must catch exactly that corruption signature, and the checked
// writer must refuse to put it on disk.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "FileScan.h"

namespace {

namespace fs = std::filesystem;

using mdboss::TextEncoding;

// The first 16 bytes of a really-corrupted document as found on disk: two
// little-endian x64 heap pointers where the title line had been.
const std::string kPointerStomp{
    "\x70\xef\x40\xc1\xe0\x01\x00\x00\x60\xb2\x0a\xc0\xe0\x01\x00\x00", 16};

std::string read_raw(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

}  // namespace

TEST_CASE("first_invalid_utf8 accepts clean document text", "[fileio]")
{
    CHECK(mdboss::first_invalid_utf8("") == std::string::npos);
    CHECK(mdboss::first_invalid_utf8("# Heading\n\nplain ascii\n") ==
          std::string::npos);
    // 2-, 3- and 4-byte sequences: é, —, 🙂
    CHECK(mdboss::first_invalid_utf8("caf\xc3\xa9 \xe2\x80\x94 "
                                     "\xf0\x9f\x99\x82") ==
          std::string::npos);
}

TEST_CASE("first_invalid_utf8 reports the corruption signature", "[fileio]")
{
    const std::string file = kPointerStomp + "\nJuly 2026\n";
    // 0x70 is fine ('p'); 0xef starts a 3-byte sequence that 0x40 breaks.
    CHECK(mdboss::first_invalid_utf8(file) == 1);
    // NUL alone is rejected even though it is structurally legal UTF-8.
    CHECK(mdboss::first_invalid_utf8(std::string("a\0b", 3)) == 1);
}

TEST_CASE("first_invalid_utf8 rejects malformed sequences", "[fileio]")
{
    CHECK(mdboss::first_invalid_utf8("\x80") == 0);          // bare cont.
    CHECK(mdboss::first_invalid_utf8("\xc1\xbf") == 0);      // overlong
    CHECK(mdboss::first_invalid_utf8("\xed\xa0\x80") == 0);  // surrogate
    CHECK(mdboss::first_invalid_utf8("\xf5\x80\x80\x80") == 0);  // > U+10FFFF
    CHECK(mdboss::first_invalid_utf8("ok\xe2\x80") == 2);    // truncated
}

TEST_CASE("detect_text_encoding recognises BOMs and heuristics", "[fileio]")
{
    CHECK(mdboss::detect_text_encoding("# plain\n") == TextEncoding::kUtf8);
    CHECK(mdboss::detect_text_encoding("") == TextEncoding::kUtf8);

    CHECK(mdboss::detect_text_encoding(std::string("\xff\xfe#\0", 4)) ==
          TextEncoding::kUtf16LE);
    CHECK(mdboss::detect_text_encoding(std::string("\xfe\xff\0#", 4)) ==
          TextEncoding::kUtf16BE);
    // BOM-less UTF-16LE: ASCII with NULs on every odd byte.
    CHECK(mdboss::detect_text_encoding(std::string("#\0 \0x\0\n\0", 8)) ==
          TextEncoding::kUtf16LE);

    // CP1252: high bytes, no NULs, not valid UTF-8 ("café" the ANSI way).
    CHECK(mdboss::detect_text_encoding("caf\xe9") == TextEncoding::kCp1252);

    // The real corruption: scattered NULs, no safe interpretation.
    CHECK(mdboss::detect_text_encoding(kPointerStomp + "\nJuly 2026\n") ==
          TextEncoding::kBinary);
}

TEST_CASE("convert_to_utf8 round-trips UTF-16", "[fileio]")
{
    bool ok = false;

    // "café" as UTF-16LE with BOM.
    const std::string le{"\xff\xfe" "c\0a\0f\0\xe9\0", 10};
    CHECK(mdboss::convert_to_utf8(le, TextEncoding::kUtf16LE, ok) ==
          "caf\xc3\xa9");
    CHECK(ok);

    // Same text big-endian, no BOM.
    const std::string be{"\0c\0a\0f\0\xe9", 8};
    CHECK(mdboss::convert_to_utf8(be, TextEncoding::kUtf16BE, ok) ==
          "caf\xc3\xa9");
    CHECK(ok);

    // An unpaired surrogate must fail loudly, not become U+FFFD.
    const std::string lone{"\x00\xd8", 2};   // U+D800 as UTF-16LE
    mdboss::convert_to_utf8(lone, TextEncoding::kUtf16LE, ok);
    CHECK_FALSE(ok);

    // Odd byte count cannot be UTF-16.
    mdboss::convert_to_utf8(std::string("a\0b", 3), TextEncoding::kUtf16LE,
                            ok);
    CHECK_FALSE(ok);
}

TEST_CASE("convert_to_utf8 handles CP1252", "[fileio]")
{
    bool ok = false;
    // Smart quotes 0x93/0x94 and em dash 0x97 -- the bytes Word documents
    // leak into pasted text.
    CHECK(mdboss::convert_to_utf8("\x93x\x94 \x97", TextEncoding::kCp1252,
                                  ok) ==
          "\xe2\x80\x9cx\xe2\x80\x9d \xe2\x80\x94");
    CHECK(ok);

    // 0x81 is undefined in CP1252 and must fail rather than be guessed at.
    mdboss::convert_to_utf8("a\x81" "b", TextEncoding::kCp1252, ok);
    CHECK_FALSE(ok);
}

TEST_CASE("write_text_file_checked writes and verifies", "[fileio]")
{
    const fs::path dir = fs::temp_directory_path() / "mdboss_fileio_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path target = dir / "doc.md";

    const std::string text = "# Title\n\ncaf\xc3\xa9 \xe2\x80\x94 done\n";
    CHECK(mdboss::write_text_file_checked(target.string(), text).empty());
    CHECK(read_raw(target) == text);

    fs::remove_all(dir, ec);
}

TEST_CASE("write_text_file_checked refuses a corrupt buffer", "[fileio]")
{
    const fs::path dir = fs::temp_directory_path() / "mdboss_fileio_test2";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path target = dir / "doc.md";

    // Put good content on disk first: the refused save must not touch it.
    const std::string good = "# intact\n";
    REQUIRE(mdboss::write_text_file_checked(target.string(), good).empty());

    const std::string corrupt = kPointerStomp + "\nJuly 2026\n";
    const std::string error =
        mdboss::write_text_file_checked(target.string(), corrupt);
    CHECK_FALSE(error.empty());
    CHECK(read_raw(target) == good);

    fs::remove_all(dir, ec);
}

TEST_CASE("write_text_file_checked reports an unwritable path", "[fileio]")
{
    const std::string error = mdboss::write_text_file_checked(
        (fs::temp_directory_path() / "mdboss_no_such_dir" / "x.md").string(),
        "# x\n");
    CHECK_FALSE(error.empty());
}
