// Version comparison and release parsing.
//
// The HTTP half is not tested here; these are the parts that decide whether
// an update is offered at all, and getting either wrong is silent -- the app
// either never updates or offers to "update" to the version already running.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Updater.h"
#include "Version.h"

namespace {

// getenv is a /W4 /WX error on MSVC; _dupenv_s is the sanctioned form.
std::string env_or_empty(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    if (::_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    const std::string out(value);
    std::free(value);
    return out;
}

}  // namespace

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
    // guess at it.  Refresh it when cutting a release:
    //   gh api repos/Flinterpop/MDBoss/releases/latest > <this file>
    // A payload from an older release keeps passing while describing a world
    // that no longer exists, which is worse than failing.
    std::ifstream stream(
        std::filesystem::path{MDBOSS_GOLDEN_DIR} / "release_latest.json",
        std::ios::binary);
    REQUIRE(stream.good());
    const std::string body((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());

    const mdboss::ReleaseInfo info = mdboss::parse_release(body);
    CHECK(info.version == std::vector<int>{1, 1, 1});
    CHECK(info.version_str == "1.1.1");
    CHECK(info.html_url ==
          "https://github.com/Flinterpop/MDBoss/releases/tag/v1.1.1");

    // v1.1.0 was the first release to carry this build's installer, so the
    // asset lookup is exercised against the real payload rather than against
    // its absence.  Several assets are present and it must pick this one --
    // MDBoss-Setup.exe is the Python installer and sorts adjacent to it.
    CHECK(info.setup_url.find("MDBoss-Cpp-Setup.exe") != std::string::npos);
    CHECK(info.setup_url.find("github.com") != std::string::npos);

    // And against this build's own version it is not an update: the fixture
    // is refreshed with each release, so this stays true and a stale fixture
    // shows up here rather than being quietly carried forward.
    const auto current = mdboss::parse_version(mdboss::kAppVersion);
    REQUIRE(current.has_value());
    CHECK_FALSE(mdboss::is_newer(info.version, *current));
}

// The handoff batch is where this feature has historically failed, in the
// Python app, twice -- and silently both times: the update simply did not
// happen. Each check below is one of those failures.
TEST_CASE("the handoff batch waits before it installs", "[updater]")
{
    const std::string batch = mdboss::installer_batch(
        "C:\\Temp\\MDBoss-Cpp-Setup.exe", "C:\\Apps\\MDBoss.exe", 4321);

    // PING, not TIMEOUT.  The batch runs with no console, where timeout exits
    // at once with "Input redirection is not supported" -- which collapsed
    // the whole wait and let the installer race the running app.
    CHECK(batch.find("PING.EXE") != std::string::npos);
    CHECK(batch.find("TIMEOUT") == std::string::npos);
    CHECK(batch.find("timeout") == std::string::npos);

    // Absolute System32 paths.  Git for Windows puts a GNU find on PATH,
    // which fails on this argument and so reports "already exited",
    // skipping the wait entirely.
    CHECK(batch.find("%SystemRoot%\\System32\\find.exe") != std::string::npos);
    CHECK(batch.find("%SystemRoot%\\System32\\tasklist.exe") !=
          std::string::npos);

    // Waits on the pid, not the image name: both builds install an exe
    // called MDBoss.exe, so waiting by name would also wait out a running
    // Python MD Boss.
    CHECK(batch.find("PID eq 4321") != std::string::npos);
    CHECK(batch.find("IMAGENAME") == std::string::npos);

    // The wait has to come before the install, or it is decoration.
    CHECK(batch.find("PID eq 4321") < batch.find("/VERYSILENT"));

    // Unattended, and it relaunches afterwards.
    CHECK(batch.find("/VERYSILENT") != std::string::npos);
    CHECK(batch.find("/NORESTART") != std::string::npos);
    CHECK(batch.find("/SUPPRESSMSGBOXES") != std::string::npos);
    CHECK(batch.find("start \"\" \"C:\\Apps\\MDBoss.exe\"") !=
          std::string::npos);

    // Relaunch after install, and cleanup after that.
    CHECK(batch.find("/VERYSILENT") < batch.find("start \"\""));
    CHECK(batch.find("start \"\"") < batch.find("del /q"));

    // cmd.exe wants CRLF, and every line must have it.
    CHECK(batch.find("\r\n") != std::string::npos);
    for (std::size_t i = 0; i + 1 < batch.size(); ++i) {
        if (batch[i] == '\n') {
            INFO("bare LF at offset " << i);
            REQUIRE(i > 0);
            CHECK(batch[i - 1] == '\r');
        }
    }

    // Paths are quoted: "C:\Program Files\..." otherwise becomes two words.
    CHECK(batch.find("\"C:\\Temp\\MDBoss-Cpp-Setup.exe\"") !=
          std::string::npos);
}

TEST_CASE("the batch gives up rather than waiting forever", "[updater]")
{
    // A process that never exits must not leave a batch spinning for good.
    const std::string batch =
        mdboss::installer_batch("C:\\s.exe", "C:\\a.exe", 7);
    CHECK(batch.find("GEQ 60") != std::string::npos);
    CHECK(batch.find("goto mdgo") != std::string::npos);
}

// Writes the real batch out so the wait can be exercised against a live
// process, which no amount of string matching above can prove.  Hidden by the
// leading dot, like the scan probe: run it deliberately with
//   mdrender_tests "[handoffprobe]"
// and MDBOSS_PROBE_PID / _SETUP / _EXE / _OUT set.
TEST_CASE("write the handoff batch for a live wait test",
          "[.][handoffprobe]")
{
    const std::string pid = env_or_empty("MDBOSS_PROBE_PID");
    const std::string setup = env_or_empty("MDBOSS_PROBE_SETUP");
    const std::string exe = env_or_empty("MDBOSS_PROBE_EXE");
    const std::string out = env_or_empty("MDBOSS_PROBE_OUT");
    REQUIRE_FALSE(pid.empty());
    REQUIRE_FALSE(setup.empty());
    REQUIRE_FALSE(exe.empty());
    REQUIRE_FALSE(out.empty());

    const std::string batch = mdboss::installer_batch(
        setup, exe, static_cast<unsigned long>(std::stoul(pid)));
    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(batch.data(), static_cast<std::streamsize>(batch.size()));
    CHECK(stream.good());
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
