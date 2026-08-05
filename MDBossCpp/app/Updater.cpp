#include "Updater.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "PathUtf8.h"


namespace mdboss {
namespace {

using json = nlohmann::json;

const char* const kReleasesUrl =
    "https://github.com/Flinterpop/MDBoss/releases";

// Bounded (Rule of 10): a tag with a hundred components is not a version.
constexpr std::size_t kMaxVersionParts = 8;

// getenv() is deprecated under /W4 /WX on MSVC; _dupenv_s is the sanctioned
// form and hands back an allocation the caller owns (same as Config.cpp).
std::string environment(const char* name)
{
    assert(name != nullptr && *name != '\0');
    char* value = nullptr;
    std::size_t size = 0;
    if (::_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    const std::string out(value);
    std::free(value);
    return out;
}

char ascii_lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

// Case-insensitive "path lives inside root" for Windows paths.  ASCII case
// folding only: the Program Files roots this is fed are ASCII.  The separator
// check keeps a sibling like "C:\Program FilesX" from matching.
bool under_root(const std::string& path, const std::string& root)
{
    if (root.empty() || path.size() <= root.size()) {
        return false;
    }
    for (std::size_t i = 0; i < root.size(); ++i) {
        if (ascii_lower(path[i]) != ascii_lower(root[i])) {
            return false;
        }
    }
    const char next = path[root.size()];
    return next == '\\' || next == '/';
}

// The wait-for-exit header shared by both handoff batches.
//
// Absolute System32 paths for every tool.  A PATH carrying GNU coreutils
// -- Git for Windows ships one -- shadows find.exe, and GNU find reads
// /I as a path and fails.  The failure looks like "the app has exited",
// so the wait is skipped and the update races the running process.
//
// PING, not TIMEOUT: these batches run with no console attached, and
// timeout exits immediately with "Input redirection is not supported",
// collapsing the whole loop.  ping -n N waits N-1 seconds and needs no
// console.  Roughly 60 seconds in total, then give up and try anyway.
std::string wait_for_exit_header(unsigned long pid)
{
    assert(pid != 0 && "a pid of 0 would match nothing and never wait");
    const std::string sys32 = "%SystemRoot%\\System32";
    const std::string pid_text = std::to_string(pid);

    std::string batch;
    batch += "@echo off\r\n";
    batch += "\"" + sys32 + "\\PING.EXE\" -n 3 127.0.0.1 >nul\r\n";
    batch += "set /a _n=0\r\n";
    batch += ":mdwait\r\n";
    batch += "\"" + sys32 + "\\tasklist.exe\" /FI \"PID eq " + pid_text +
             "\" 2>nul | \"" + sys32 + "\\find.exe\" \"" + pid_text +
             "\" >nul\r\n";
    batch += "if errorlevel 1 goto mdgo\r\n";
    batch += "set /a _n+=1\r\n";
    batch += "if %_n% GEQ 60 goto mdgo\r\n";
    batch += "\"" + sys32 + "\\PING.EXE\" -n 2 127.0.0.1 >nul\r\n";
    batch += "goto mdwait\r\n";
    batch += ":mdgo\r\n";
    return batch;
}

}  // namespace

const char* const kSetupAssetName = "MDBoss-Cpp-Setup.exe";
const char* const kPortableAssetName = "MDBoss-Cpp-Portable.zip";

std::optional<std::vector<int>> parse_version(const std::string& text)
{
    std::size_t at = 0;
    while (at < text.size() && (text[at] == 'v' || text[at] == 'V')) {
        ++at;
    }
    if (at >= text.size()) {
        return std::nullopt;
    }

    std::vector<int> parts;
    int value = 0;
    bool have_digit = false;
    for (; at <= text.size(); ++at) {
        const char ch = (at < text.size()) ? text[at] : '.';
        if (ch >= '0' && ch <= '9') {
            // Cap rather than overflow on a silly tag.
            value = (value > 100000) ? value : value * 10 + (ch - '0');
            have_digit = true;
        } else if (ch == '.') {
            if (!have_digit || parts.size() >= kMaxVersionParts) {
                return std::nullopt;
            }
            parts.push_back(value);
            value = 0;
            have_digit = false;
        } else {
            return std::nullopt;   // any other character makes it unusable
        }
    }
    if (parts.empty()) {
        return std::nullopt;
    }
    return parts;
}

bool is_newer(const std::vector<int>& candidate,
              const std::vector<int>& current)
{
    const std::size_t count = std::max(candidate.size(), current.size());
    for (std::size_t i = 0; i < count; ++i) {
        // A missing component is 0, so 1.2 and 1.2.0 compare equal and
        // 1.2 < 1.2.1 rather than being incomparable.
        const int a = (i < candidate.size()) ? candidate[i] : 0;
        const int b = (i < current.size()) ? current[i] : 0;
        if (a != b) {
            return a > b;
        }
    }
    return false;
}

ReleaseInfo parse_release(const std::string& json_body)
{
    ReleaseInfo info;
    info.html_url = kReleasesUrl;

    const json document = json::parse(json_body, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return info;
    }
    if (document.contains("html_url") && document["html_url"].is_string()) {
        info.html_url = document["html_url"].get<std::string>();
    }

    std::string tag;
    if (document.contains("tag_name") && document["tag_name"].is_string()) {
        tag = document["tag_name"].get<std::string>();
    }
    const std::optional<std::vector<int>> version = parse_version(tag);
    if (!version) {
        return info;   // no version: the caller treats this as "cannot tell"
    }
    info.version = *version;
    info.version_str = tag;
    while (!info.version_str.empty() &&
           (info.version_str.front() == 'v' || info.version_str.front() == 'V')) {
        info.version_str.erase(info.version_str.begin());
    }

    if (document.contains("assets") && document["assets"].is_array()) {
        for (const json& asset : document["assets"]) {
            if (!asset.is_object() || !asset.contains("name") ||
                !asset["name"].is_string() ||
                !asset.contains("browser_download_url") ||
                !asset["browser_download_url"].is_string()) {
                continue;
            }
            const std::string name = asset["name"].get<std::string>();
            const std::string url =
                asset["browser_download_url"].get<std::string>();
            if (name == kSetupAssetName) {
                info.setup_url = url;
            } else if (name == kPortableAssetName) {
                info.portable_url = url;
            }
        }
    }
    return info;
}

bool portable_install(const std::string& exe_dir_utf8)
{
    assert(!exe_dir_utf8.empty() && "no directory to inspect");
    std::error_code ec;
    std::filesystem::directory_iterator it(path_from_utf8(exe_dir_utf8), ec);
    if (ec) {
        return true;   // unreadable: the same lean app.py takes
    }
    const std::filesystem::directory_iterator end;
    // Bounded (Rule of 10): an install folder holds dozens of entries, not
    // tens of thousands; give up counting long before that.
    for (int seen = 0; it != end && seen < 10000; it.increment(ec), ++seen) {
        if (ec) {
            return true;
        }
        std::string name = path_to_utf8(it->path().filename());
        for (char& ch : name) {
            ch = ascii_lower(ch);
        }
        if (name.size() >= 9 && name.starts_with("unins") &&
            name.ends_with(".exe")) {
            return false;   // an Inno uninstaller: this is an installed copy
        }
    }
    return true;
}

std::string install_scope_flag(const std::string& app_exe)
{
    assert(!app_exe.empty() && "no exe path to classify");
    // ProgramW6432 covers a 32-bit process on 64-bit Windows; the (x86) root
    // covers an exe that was installed there anyway.
    const char* const roots[] = {"ProgramFiles", "ProgramW6432",
                                 "ProgramFiles(x86)"};
    for (const char* const name : roots) {
        if (under_root(app_exe, environment(name))) {
            return "/ALLUSERS";
        }
    }
    return "/CURRENTUSER";
}

std::string installer_batch(const std::string& setup_path,
                            const std::string& app_exe, unsigned long pid)
{
    assert(!setup_path.empty() && "nothing to install");
    assert(!app_exe.empty() && "nothing to relaunch");

    std::string batch = wait_for_exit_header(pid);
    batch += "\"" + setup_path +
             "\" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES " +
             install_scope_flag(app_exe) + "\r\n";
    batch += "start \"\" \"" + app_exe + "\"\r\n";
    batch += "del /q \"" + setup_path + "\"\r\n";
    batch += "del /q \"%~f0\"\r\n";
    return batch;
}

std::string portable_batch(const std::string& zip_path,
                           const std::string& staging_dir,
                           const std::string& app_exe, unsigned long pid)
{
    assert(!zip_path.empty() && "nothing to unpack");
    assert(!staging_dir.empty() && "nowhere to unpack to");
    assert(!app_exe.empty() && "nothing to relaunch");
    const std::size_t cut = app_exe.find_last_of("\\/");
    assert(cut != std::string::npos && "app_exe must include its folder");
    const std::string app_dir = app_exe.substr(0, cut);

    const std::string sys32 = "%SystemRoot%\\System32";

    // Extract, check, and only then copy.  A zip with no MDBoss.exe -- at
    // the root or one folder down, the two layouts app.py accepts -- copies
    // nothing, and the relaunch line runs either way: a failed update is a
    // no-op, not a brick.  robocopy copies OVER the install rather than
    // replacing it, for the same reason as _portable_batch in app.py.
    std::string batch = wait_for_exit_header(pid);
    batch += "md \"" + staging_dir + "\" 2>nul\r\n";
    batch += "\"" + sys32 + "\\tar.exe\" -xf \"" + zip_path + "\" -C \"" +
             staging_dir + "\"\r\n";
    batch += "set \"_src=\"\r\n";
    batch += "if exist \"" + staging_dir +
             "\\MDBoss.exe\" set \"_src=" + staging_dir + "\"\r\n";
    batch += "if exist \"" + staging_dir +
             "\\MDBoss\\MDBoss.exe\" set \"_src=" + staging_dir +
             "\\MDBoss\"\r\n";
    batch += "if \"%_src%\"==\"\" goto mdrelaunch\r\n";
    batch += "robocopy \"%_src%\" \"" + app_dir +
             "\" /E /IS /IT /R:2 /W:2 /NFL /NDL /NJH /NJS /NP >nul\r\n";
    batch += ":mdrelaunch\r\n";
    batch += "start \"\" \"" + app_exe + "\"\r\n";
    batch += "rd /s /q \"" + staging_dir + "\"\r\n";
    batch += "del /q \"" + zip_path + "\"\r\n";
    batch += "del /q \"%~f0\"\r\n";
    return batch;
}

}  // namespace mdboss
