// Tests for new-document template substitution.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "LogoAsset.h"
#include "Templates.h"

namespace {

namespace fs = std::filesystem;

std::string read_raw(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// A tech-note banner line as the TechNote starter writes it.
std::string banner()
{
    return "<img src=\"" + mdboss::logo_data_uri() +
           "\" alt=\"image-20240901145033347\" style=\"zoom: 50%;\" />"
           " TN 2026-0X\n";
}

// A fixed local time, so the expected output is not a moving target.
std::tm fixed_time()
{
    std::tm when{};
    when.tm_year = 126;   // 2026
    when.tm_mon = 6;      // July
    when.tm_mday = 28;
    when.tm_hour = 9;
    when.tm_min = 5;
    when.tm_sec = 0;
    when.tm_isdst = -1;
    return when;
}

}  // namespace

TEST_CASE("placeholders are substituted", "[templates]")
{
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("# {{title}}", "Report", when) == "# Report");
    CHECK(mdboss::apply_template("{{date}}", "x", when) == "2026-07-28");
    CHECK(mdboss::apply_template("{{time}}", "x", when) == "09:05");
    CHECK(mdboss::apply_template("{{datetime}}", "x", when) ==
          "2026-07-28 09:05");
}

TEST_CASE("no placeholder is a substring of another", "[templates]")
{
    // {{datetime}} must survive the {{date}} and {{time}} passes intact.  If
    // a future placeholder overlaps an existing one, substitution order
    // starts to matter and this catches it.
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("{{datetime}}|{{date}}|{{time}}", "x",
                                 when) ==
          "2026-07-28 09:05|2026-07-28|09:05");
}

TEST_CASE("every occurrence is replaced", "[templates]")
{
    const std::tm when = fixed_time();
    // The "Document" starter uses {{title}} twice, once in front matter and
    // once as the heading.
    CHECK(mdboss::apply_template("{{title}} {{title}} {{title}}", "A", when) ==
          "A A A");
}

TEST_CASE("text with no placeholders is untouched", "[templates]")
{
    const std::tm when = fixed_time();
    const std::string text = "# Plain\n\nNothing to substitute { } {{ }}.\n";
    CHECK(mdboss::apply_template(text, "ignored", when) == text);
}

TEST_CASE("an empty title substitutes as empty, not as the token",
          "[templates]")
{
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("[{{title}}]", "", when) == "[]");
}

TEST_CASE("{{guid}} becomes a v4 UUID, fresh every time", "[templates][guid]")
{
    // A tech note carries a GUID so two notes can be told apart; an identifier
    // that repeated would be worse than none.  Unlike every other placeholder
    // this one is NOT a function of the timestamp, so it is checked with the
    // deterministic overload -- two notes created in the same minute must
    // still differ.
    std::tm when{};
    when.tm_year = 126;   // 2026
    when.tm_mon = 7;
    when.tm_mday = 17;

    const std::string a = mdboss::apply_template("{{guid}}", "T", when);
    const std::string b = mdboss::apply_template("{{guid}}", "T", when);

    REQUIRE(a.size() == 36);
    CHECK(a != b);

    // 8-4-4-4-12, lower-case hex, with the version and variant nibbles that
    // make it a well-formed random UUID rather than 32 loose hex digits.
    CHECK(a[8] == '-');
    CHECK(a[13] == '-');
    CHECK(a[18] == '-');
    CHECK(a[23] == '-');
    CHECK(a[14] == '4');                       // version 4
    CHECK(std::string("89ab").find(a[19]) != std::string::npos);   // variant
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        INFO("index " << i << " char '" << a[i] << "'");
        CHECK(std::string("0123456789abcdef").find(a[i]) != std::string::npos);
    }
}

TEST_CASE("the tech-note starter carries the house front matter",
          "[templates][technote]")
{
    // The header is the thing people notice when it is wrong, and it is
    // assembled from a string literal that nothing else checks.
    const std::string note =
        mdboss::apply_template(mdboss::technote_template(), "My Note");

    CHECK(note.rfind("---\n", 0) == 0);
    CHECK(note.find("\ntitle: My Note\n") != std::string::npos);
    CHECK(note.find("\nauthor: B. Graham\n") != std::string::npos);
    CHECK(note.find("\nversion: 0.1\n") != std::string::npos);
    CHECK(note.find("\nkeywords: TechNote\n") != std::string::npos);
    // Filled, not left as the token.
    CHECK(note.find("{{") == std::string::npos);
    const std::size_t guid = note.find("\nGUID: ");
    REQUIRE(guid != std::string::npos);
    CHECK(note.substr(guid + 7, 36).find('-') != std::string::npos);
}

TEST_CASE("{{year}} fills the tech-note number", "[templates]")
{
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("TN {{year}}-0X", "x", when) ==
          "TN 2026-0X");
    // It must not disturb the date placeholders it shares a prefix-free
    // alphabet with.
    CHECK(mdboss::apply_template("{{year}}|{{date}}|{{datetime}}", "x", when) ==
          "2026|2026-07-28|2026-07-28 09:05");
}

// ---- The embedded tech-note logo -----------------------------------------

TEST_CASE("the embedded logo decodes back to the artwork", "[templates]")
{
    const std::vector<unsigned char> bytes = mdboss::logo_png_bytes();
    REQUIRE(bytes.size() == mdboss::kLogoPngBytes);
    // A PNG signature, so a blob that decoded to *something* of the right
    // length still has to be an image.
    REQUIRE(bytes.size() > 8);
    const unsigned char signature[8] = {0x89, 'P',  'N',  'G',
                                        0x0D, 0x0A, 0x1A, 0x0A};
    for (std::size_t i = 0; i < sizeof(signature); ++i) {
        CHECK(bytes[i] == signature[i]);
    }
}

TEST_CASE("the data URI is a complete img src value", "[templates]")
{
    const std::string uri = mdboss::logo_data_uri();
    CHECK(uri.rfind("data:image/png;base64,", 0) == 0);
    // Long enough to be the artwork rather than a truncated literal, and free
    // of the quote that would break out of the src attribute.
    CHECK(uri.size() > 6000);
    CHECK(uri.find('"') == std::string::npos);
}

TEST_CASE("only the logo this app embeds counts as embedded", "[templates]")
{
    CHECK(mdboss::has_embedded_logo(banner()));
    CHECK_FALSE(mdboss::has_embedded_logo("# Plain note\n"));
    // A data: URI the user wrote is somebody else's image; leave it alone.
    CHECK_FALSE(mdboss::has_embedded_logo(
        "<img src=\"data:image/png;base64,iVBORw0KGgo=\" />"));
}

TEST_CASE("localizing writes the png and swaps in the relative reference",
          "[templates]")
{
    const fs::path dir = fs::temp_directory_path() / "mdboss_logo_swap";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path note = dir / "note.md";

    const std::string before = "---\nA Note\n---\n" + banner() + "\nB. Graham\n";
    const std::string after =
        mdboss::localize_embedded_logo(before, note.string());

    // The document now points at the file, and nothing else about the banner
    // moved -- the alt text, the zoom style and the note number all survive.
    CHECK(after.find("src=\"background-logo.png\"") != std::string::npos);
    CHECK(after.find("data:image") == std::string::npos);
    CHECK(after.find("alt=\"image-20240901145033347\"") != std::string::npos);
    CHECK(after.find("style=\"zoom: 50%;\"") != std::string::npos);
    CHECK(after.find("TN 2026-0X") != std::string::npos);

    // ...and the file it points at is the artwork, byte for byte.
    const fs::path png = dir / mdboss::kLogoFileName;
    REQUIRE(fs::exists(png, ec));
    const std::string written = read_raw(png);
    const std::vector<unsigned char> expected = mdboss::logo_png_bytes();
    REQUIRE(written.size() == expected.size());
    CHECK(std::equal(expected.begin(), expected.end(), written.begin(),
                     [](unsigned char a, char b) {
                         return a == static_cast<unsigned char>(b);
                     }));

    fs::remove_all(dir, ec);
}

TEST_CASE("localizing never overwrites a logo already in the folder",
          "[templates]")
{
    // A folder's existing background-logo.png is the one its other notes
    // already point at; replacing it would silently restyle all of them.
    const fs::path dir = fs::temp_directory_path() / "mdboss_logo_keep";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const fs::path png = dir / mdboss::kLogoFileName;
    {
        std::ofstream stream(png, std::ios::binary);
        stream << "not really a png";
    }

    const std::string after = mdboss::localize_embedded_logo(
        banner(), (dir / "note.md").string());

    CHECK(after.find("src=\"background-logo.png\"") != std::string::npos);
    CHECK(read_raw(png) == "not really a png");

    fs::remove_all(dir, ec);
}

TEST_CASE("localizing leaves a document with no embedded logo alone",
          "[templates]")
{
    const fs::path dir = fs::temp_directory_path() / "mdboss_logo_none";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const std::string text = "# Ordinary note\n\nNo banner here.\n";
    CHECK(mdboss::localize_embedded_logo(text, (dir / "n.md").string()) ==
          text);
    // No stray .png in a folder that never asked for one.
    CHECK_FALSE(fs::exists(dir / mdboss::kLogoFileName, ec));

    fs::remove_all(dir, ec);
}

TEST_CASE("localizing is idempotent", "[templates]")
{
    // The save path runs on every save, not just the first.
    const fs::path dir = fs::temp_directory_path() / "mdboss_logo_twice";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string note = (dir / "note.md").string();

    const std::string once = mdboss::localize_embedded_logo(banner(), note);
    const std::string twice = mdboss::localize_embedded_logo(once, note);
    CHECK(once == twice);

    fs::remove_all(dir, ec);
}
