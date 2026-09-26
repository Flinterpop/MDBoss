// AppIdentity: MD Boss's defaults are the literals it always had, and an
// embedding app (DocBoss) that sets its own identity gets its own profile,
// ProgID, release stream and exe check -- never MD Boss's.
//
// Each of these would fail silently in the embedding app: it would read and
// write MD Boss's profile, or update itself from MD Boss's releases.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "AppIdentity.h"
#include "FileAssoc.h"
#include "PathUtf8.h"
#include "Templates.h"
#include "Updater.h"

namespace {

namespace fs = std::filesystem;

// Restores MD Boss's identity however the test leaves, so a failure here
// cannot leak a foreign identity into every test that runs after it.
class IdentityGuard {
public:
    explicit IdentityGuard(const mdboss::AppIdentity& identity)
    {
        mdboss::set_app_identity(identity);
    }
    ~IdentityGuard() { mdboss::set_app_identity(mdboss::AppIdentity{}); }
    IdentityGuard(const IdentityGuard&) = delete;
    IdentityGuard& operator=(const IdentityGuard&) = delete;
};

mdboss::AppIdentity other_app(const std::string& data_dir)
{
    mdboss::AppIdentity identity;
    identity.display_name = "Other App";
    identity.data_dir = data_dir;
    identity.prog_id = "OtherApp.Markdown";
    identity.assoc_display_name = "Other App";
    identity.assoc_app_name = "OtherApp";
    identity.instance_prop = L"OtherApp.Instance";
    identity.instance_mutex = L"Local\\OtherApp.SingleInstance";
    identity.releases_api_url =
        "https://api.github.com/repos/Example/OtherApp/releases/latest";
    identity.releases_page_url = "https://github.com/Example/OtherApp/releases";
    identity.setup_asset = "OtherApp-Setup.exe";
    identity.portable_asset = "OtherApp-Portable.zip";
    identity.exe_name = "OtherApp.exe";
    identity.portable_folder = "OtherApp";
    return identity;
}

const char* const kRelease = R"({
    "tag_name": "v9.9.9",
    "assets": [
        {"name": "MDBoss-Cpp-Setup.exe",
         "browser_download_url": "https://example.invalid/md-setup"},
        {"name": "OtherApp-Setup.exe",
         "browser_download_url": "https://example.invalid/other-setup"},
        {"name": "OtherApp-Portable.zip",
         "browser_download_url": "https://example.invalid/other-zip"}
    ]
})";

}  // namespace

TEST_CASE("MD Boss's identity is the one it always had", "[identity]")
{
    const mdboss::AppIdentity& identity = mdboss::app_identity();
    CHECK(identity.display_name == "MD Boss");
    CHECK(identity.data_dir.empty());
    CHECK(identity.staging_dir.empty());
    CHECK(identity.prog_id == "MDBoss.Markdown");
    CHECK(identity.instance_mutex == L"Local\\MDBossCpp.SingleInstance");
    CHECK(identity.setup_asset == "MDBoss-Cpp-Setup.exe");
    CHECK(identity.portable_asset == "MDBoss-Cpp-Portable.zip");
    CHECK(identity.exe_name == "MDBoss.exe");
    CHECK(fs::path(mdboss::path_from_utf8(mdboss::user_data_dir()))
              .filename() == "MDBoss");
}

TEST_CASE("another identity picks its own release assets", "[identity]")
{
    const IdentityGuard guard(other_app("unused"));
    const mdboss::ReleaseInfo info = mdboss::parse_release(kRelease);
    CHECK(info.setup_url == "https://example.invalid/other-setup");
    CHECK(info.portable_url == "https://example.invalid/other-zip");
    CHECK(info.html_url == "https://github.com/Example/OtherApp/releases");
}

TEST_CASE("another identity's portable update checks for its own exe",
          "[identity]")
{
    const IdentityGuard guard(other_app("unused"));
    const std::string batch = mdboss::portable_batch(
        "C:\\Temp\\o.zip", "C:\\Temp\\o.new", "C:\\Apps\\OtherApp.exe", 7);
    CHECK(batch.find("\\OtherApp.exe\" set") != std::string::npos);
    CHECK(batch.find("\\OtherApp\\OtherApp.exe\" set") != std::string::npos);
    CHECK(batch.find("MDBoss") == std::string::npos);
}

TEST_CASE("another identity registers its own ProgID", "[identity]")
{
    const IdentityGuard guard(other_app("unused"));
    const mdboss::RegPlan plan = mdboss::registration_plan(
        "\"C:\\Apps\\OtherApp.exe\" \"%1\"", "C:\\Apps\\OtherApp.exe,0",
        "OtherApp.exe");
    bool saw_progid = false;
    for (const auto& value : plan.values) {
        CHECK(value.key.find("MDBoss") == std::string::npos);
        if (value.key == "Software\\Classes\\OtherApp.Markdown") {
            saw_progid = true;
        }
    }
    CHECK(saw_progid);
}

TEST_CASE("another identity keeps its templates in its own profile",
          "[identity]")
{
    const fs::path data = fs::temp_directory_path() / "mdboss_identity_test";
    std::error_code ec;
    fs::remove_all(data, ec);
    {
        const IdentityGuard guard(other_app(mdboss::path_to_utf8(data)));
        CHECK(mdboss::user_data_dir() == mdboss::path_to_utf8(data));
        CHECK(mdboss::path_from_utf8(mdboss::templates_dir()) ==
              data / "templates");

        mdboss::SeededTemplates seeded;
        CHECK(mdboss::seed_templates(seeded));
        CHECK(seeded.known);
        CHECK_FALSE(seeded.names.empty());
        CHECK_FALSE(mdboss::list_templates().empty());

        // Offered once: a second pass adds nothing.
        CHECK_FALSE(mdboss::seed_templates(seeded));
    }
    fs::remove_all(data, ec);
}
