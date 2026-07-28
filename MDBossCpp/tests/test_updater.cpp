// Version comparison and release parsing.
//
// The HTTP half is not tested here; these are the parts that decide whether
// an update is offered at all, and getting either wrong is silent -- the app
// either never updates or offers to "update" to the version already running.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Updater.h"
#include "Version.h"

TEST_CASE("version tags parse", "[updater]")
{
    using V = std::vector<int>;
    CHECK(mdboss::parse_version("1.2.3") == V{1, 2, 3});
    CHECK(mdboss::parse_version("v1.2.3") == V{1, 2, 3});
    CHECK(mdboss::parse_version("V1.0.0") == V{1, 0, 0});
    CHECK(mdboss::parse_version("2") == V{2});
    CHECK(mdboss::parse_version("1.0") == V{1, 0});
}

TEST_CASE("malformed tags parse to nothing, not to zero", "[updater]")
{
    // Returning {0,0,0} for junk would make every release look older than
    // the running build, so updates would silently never be offered.
    CHECK_FALSE(mdboss::parse_version("").has_value());
    CHECK_FALSE(mdboss::parse_version("v").has_value());
    CHECK_FALSE(mdboss::parse_version("1.2.beta").has_value());
    CHECK_FALSE(mdboss::parse_version("1..2").has_value());
    CHECK_FALSE(mdboss::parse_version("1.2.").has_value());
    CHECK_FALSE(mdboss::parse_version("release-1.2").has_value());
}

TEST_CASE("newer is decided component by component", "[updater]")
{
    CHECK(mdboss::is_newer({1, 0, 1}, {1, 0, 0}));
    CHECK(mdboss::is_newer({1, 1, 0}, {1, 0, 9}));
    CHECK(mdboss::is_newer({2, 0, 0}, {1, 9, 9}));
    CHECK_FALSE(mdboss::is_newer({1, 0, 0}, {1, 0, 0}));
    CHECK_FALSE(mdboss::is_newer({1, 0, 0}, {1, 0, 1}));
    // Numeric, not lexical: 10 is above 9, which string comparison gets wrong.
    CHECK(mdboss::is_newer({1, 10, 0}, {1, 9, 0}));
}

TEST_CASE("missing components count as zero", "[updater]")
{
    CHECK_FALSE(mdboss::is_newer({1, 2}, {1, 2, 0}));
    CHECK_FALSE(mdboss::is_newer({1, 2, 0}, {1, 2}));
    CHECK(mdboss::is_newer({1, 2, 1}, {1, 2}));
}

TEST_CASE("a release payload yields tag and matching asset", "[updater]")
{
    const std::string body = R"({
      "tag_name": "v1.2.0",
      "html_url": "https://github.com/Flinterpop/MDBoss/releases/tag/v1.2.0",
      "assets": [
        {"name": "MDBoss-Setup.exe",
         "browser_download_url": "https://example.invalid/python.exe"},
        {"name": "MDBoss-Cpp-Setup.exe",
         "browser_download_url": "https://example.invalid/cpp.exe"},
        {"name": "MDBoss-Portable-App.zip",
         "browser_download_url": "https://example.invalid/portable.zip"}
      ]
    })";

    const mdboss::ReleaseInfo info = mdboss::parse_release(body);
    CHECK(info.version == std::vector<int>{1, 2, 0});
    CHECK(info.version_str == "1.2.0");          // the leading v is stripped
    // Must pick THIS build's installer, not the Python one that sorts first.
    CHECK(info.setup_url == "https://example.invalid/cpp.exe");
    CHECK(info.html_url ==
          "https://github.com/Flinterpop/MDBoss/releases/tag/v1.2.0");
}

TEST_CASE("a release with no matching asset still reports its version",
          "[updater]")
{
    // The app can then offer the releases page instead of doing nothing.
    const std::string body = R"({
      "tag_name": "v9.9.9",
      "assets": [{"name": "MDBoss-Setup.exe",
                  "browser_download_url": "https://example.invalid/other"}]
    })";
    const mdboss::ReleaseInfo info = mdboss::parse_release(body);
    CHECK(info.version == std::vector<int>{9, 9, 9});
    CHECK(info.setup_url.empty());
    CHECK_FALSE(info.html_url.empty());   // falls back to the releases page
}

TEST_CASE("the real GitHub payload parses as expected", "[updater]")
{
    // golden/release_latest.json is the actual response from
    // api.github.com/repos/Flinterpop/MDBoss/releases/latest, kept so the
    // parser is pinned against the real shape rather than a hand-written
    // guess at it.
    std::ifstream stream(
        std::filesystem::path{MDBOSS_GOLDEN_DIR} / "release_latest.json",
        std::ios::binary);
    REQUIRE(stream.good());
    const std::string body((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());

    const mdboss::ReleaseInfo info = mdboss::parse_release(body);
    CHECK(info.version == std::vector<int>{1, 0, 0});
    CHECK(info.version_str == "1.0.0");
    CHECK(info.html_url ==
          "https://github.com/Flinterpop/MDBoss/releases/tag/v1.0.0");

    // That release carries the Python installer, the portable zip and the
    // AppImage, but no C++ installer -- this port has never been released.
    // So the app must fall back to offering the page, not fail.
    CHECK(info.setup_url.empty());

    // And against this build's own version it is not an update.
    const auto current = mdboss::parse_version(mdboss::kAppVersion);
    REQUIRE(current.has_value());
    CHECK_FALSE(mdboss::is_newer(info.version, *current));
}

TEST_CASE("junk payloads are handled, not trusted", "[updater]")
{
    for (const std::string& body :
         {std::string("not json at all"), std::string("[]"),
          std::string("{}"), std::string(R"({"tag_name": "nightly"})")}) {
        const mdboss::ReleaseInfo info = mdboss::parse_release(body);
        INFO("body: " << body);
        CHECK(info.version.empty());       // "cannot tell", never "0.0.0"
        CHECK_FALSE(info.html_url.empty());
    }
}
