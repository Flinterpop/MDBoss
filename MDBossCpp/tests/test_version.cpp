// Version lockstep.
//
// The repo's convention is that one version number is bumped across every
// app, installer and resource in a single commit.  Missing one produces an
// installer or an exe whose reported version disagrees with the release --
// and in the numeric FILEVERSION case, an installer that quietly refuses to
// upgrade.
//
// Until now nothing enforced that; it was a rule people remembered.  This
// reads the four places the number actually lives and fails if they disagree.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Version.h"

namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

// The text between `open` and the next `close`, or empty.
std::string between(const std::string& text, const std::string& open,
                    char close)
{
    const std::size_t start = text.find(open);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t from = start + open.size();
    const std::size_t end = text.find(close, from);
    if (end == std::string::npos) {
        return {};
    }
    return text.substr(from, end - from);
}

fs::path repo_root()
{
    return fs::path{MDBOSS_REPO_DIR};
}

}  // namespace

TEST_CASE("version is in lockstep across the repo", "[version]")
{
    const std::string expected = mdboss::kAppVersion;
    REQUIRE_FALSE(expected.empty());

    // app.py: APP_VERSION = "1.0.0"
    const std::string app_py = read_file(repo_root() / "app.py");
    REQUIRE_FALSE(app_py.empty());
    CHECK(between(app_py, "APP_VERSION = \"", '"') == expected);

    // installer.iss: #define AppVersion "1.0.0"
    const std::string iss = read_file(repo_root() / "installer.iss");
    REQUIRE_FALSE(iss.empty());
    CHECK(between(iss, "#define AppVersion \"", '"') == expected);

    // MDBoss.rc carries the version twice, and both forms matter: Explorer
    // shows the string, installers compare the numeric tuple.
    const std::string rc =
        read_file(repo_root() / "MDBossCpp" / "app" / "MDBoss.rc");
    REQUIRE_FALSE(rc.empty());
    CHECK(between(rc, "VALUE \"FileVersion\",      \"", '"') == expected);
    CHECK(between(rc, "VALUE \"ProductVersion\",   \"", '"') == expected);

    // The numeric tuple is the string with dots turned into commas, plus a
    // trailing build field: 1.0.0 -> 1,0,0,0.
    std::string numeric = expected;
    for (char& ch : numeric) {
        if (ch == '.') {
            ch = ',';
        }
    }
    numeric += ",0";
    CHECK(between(rc, "FILEVERSION    ", '\n').find(numeric) == 0);
    CHECK(between(rc, "PRODUCTVERSION ", '\n').find(numeric) == 0);
}
