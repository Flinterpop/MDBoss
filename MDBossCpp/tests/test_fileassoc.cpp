// Parity test for the Windows registration table.
//
// Both builds claim the same ProgID, so if their registration plans drift the
// one that ran last leaves a half-updated set of keys behind -- some pointing
// at the Python app, some at the C++ one.  golden/regplan.tsv is generated
// from the Python app's registration_plan() with fixed inputs; this checks the
// C++ plan matches it row for row.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "FileAssoc.h"

namespace {

std::vector<std::string> read_lines(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

// Serialise the C++ plan in the same shape make_golden.py writes.
std::vector<std::string> serialise(const mdboss::RegPlan& plan)
{
    std::vector<std::string> lines;
    for (const mdboss::RegValue& value : plan.values) {
        lines.push_back("value\t" + value.key + "\t" + value.name + "\t" +
                        value.data);
    }
    for (const std::string& key : plan.owned_keys) {
        lines.push_back("owned\t" + key);
    }
    for (const mdboss::RegSharedValue& shared : plan.shared_values) {
        lines.push_back("shared\t" + shared.key + "\t" + shared.name);
    }
    return lines;
}

}  // namespace

TEST_CASE("registration plan matches the Python app", "[assoc]")
{
    const std::filesystem::path golden =
        std::filesystem::path{MDBOSS_GOLDEN_DIR} / "regplan.tsv";
    const std::vector<std::string> expected = read_lines(golden);
    const std::vector<std::string> actual = serialise(
        mdboss::registration_plan("\"CMD.EXE\" \"%1\"", "ICON.ico,0",
                                  "EXE.exe"));

    REQUIRE(expected.size() == actual.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("row " << i);
        CHECK(actual[i] == expected[i]);
    }
}

TEST_CASE("every owned key is deleted deepest first", "[assoc]")
{
    // Deleting a parent before its child fails on Windows, leaving the whole
    // subtree behind, so ordering here is load bearing rather than tidy.
    const mdboss::RegPlan plan = mdboss::registration_plan(
        "\"CMD.EXE\" \"%1\"", "ICON.ico,0", "EXE.exe");

    for (std::size_t i = 0; i < plan.owned_keys.size(); ++i) {
        for (std::size_t j = i + 1; j < plan.owned_keys.size(); ++j) {
            const std::string& earlier = plan.owned_keys[i];
            const std::string& later = plan.owned_keys[j];
            // `later` must not be a strict child of `earlier`.
            const bool later_is_child =
                later.size() > earlier.size() &&
                later.compare(0, earlier.size(), earlier) == 0 &&
                later[earlier.size()] == '\\';
            INFO(earlier << " is deleted before its child " << later);
            CHECK_FALSE(later_is_child);
        }
    }
}

// Exercises the real RegCreateKeyEx/RegSetValueEx/RegDeleteKey path, but on a
// scratch key this test owns outright.
//
// Deliberately NOT the real plan: the installed Python build owns the
// MDBoss.Markdown ProgID on a developer machine, so applying the real plan
// would repoint the user's .md association at a build directory, and removing
// it would delete the installed app's registration. The layout is covered by
// the golden comparison above; this covers the writing.
TEST_CASE("registration writes and cleans up after itself", "[assoc][registry]")
{
    const std::string root = "Software\\MDBossCppTest";
    mdboss::RegPlan plan;
    plan.values = {
        {root, "Plain", "value-a"},
        {root + "\\Child", "", "default-value"},
        {root + "\\Child\\Deeper", "Nested", "value-b"},
    };
    plan.owned_keys = {root + "\\Child\\Deeper", root + "\\Child", root};

    const auto read = [](const std::string& key,
                         const char* name) -> std::string {
        HKEY handle = nullptr;
        const std::wstring wkey(key.begin(), key.end());
        if (RegOpenKeyExW(HKEY_CURRENT_USER, wkey.c_str(), 0, KEY_QUERY_VALUE,
                          &handle) != ERROR_SUCCESS) {
            return "<no key>";
        }
        wchar_t buffer[512]{};
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        std::wstring wname;
        if (name != nullptr) {
            const std::string n(name);
            wname.assign(n.begin(), n.end());
        }
        const LSTATUS status = RegQueryValueExW(
            handle, name == nullptr ? nullptr : wname.c_str(), nullptr, &type,
            reinterpret_cast<BYTE*>(buffer), &size);
        RegCloseKey(handle);
        if (status != ERROR_SUCCESS) {
            return "<no value>";
        }
        // Explicit cast: the iterator-pair constructor narrows wchar_t to
        // char implicitly, which is a C4244 error under /W4 /WX.  These are
        // ASCII test values, so a byte-wise cast is exact.
        std::string out;
        for (const wchar_t ch : std::wstring(buffer)) {
            out += static_cast<char>(ch);
        }
        return out;
    };

    REQUIRE(mdboss::apply_registration(plan));
    CHECK(read(root, "Plain") == "value-a");
    CHECK(read(root + "\\Child", nullptr) == "default-value");
    CHECK(read(root + "\\Child\\Deeper", "Nested") == "value-b");

    mdboss::remove_registration(plan);
    // Deepest-first ordering is what makes this succeed: deleting the parent
    // first would fail and strand the subtree.
    CHECK(read(root, "Plain") == "<no key>");
    CHECK(read(root + "\\Child", nullptr) == "<no key>");
    CHECK(read(root + "\\Child\\Deeper", "Nested") == "<no key>");
}

TEST_CASE("the plan always passes the file through", "[assoc]")
{
    const mdboss::RegPlan plan = mdboss::registration_plan(
        "\"CMD.EXE\" \"%1\"", "ICON.ico,0", "EXE.exe");

    // Whatever else changes, the shell must hand the document to the app --
    // a command without %1 silently opens the app with no file.
    bool found_command = false;
    for (const mdboss::RegValue& value : plan.values) {
        if (value.key.find("shell\\open\\command") != std::string::npos) {
            found_command = true;
            CHECK(value.data.find("\"%1\"") != std::string::npos);
        }
    }
    CHECK(found_command);
}
