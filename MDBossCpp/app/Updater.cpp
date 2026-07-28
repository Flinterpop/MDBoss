#include "Updater.h"

#include <nlohmann/json.hpp>


namespace mdboss {
namespace {

using json = nlohmann::json;

const char* const kReleasesUrl =
    "https://github.com/Flinterpop/MDBoss/releases";

// Bounded (Rule of 10): a tag with a hundred components is not a version.
constexpr std::size_t kMaxVersionParts = 8;

}  // namespace

const char* const kSetupAssetName = "MDBoss-Cpp-Setup.exe";

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
                !asset["name"].is_string()) {
                continue;
            }
            if (asset["name"].get<std::string>() != kSetupAssetName) {
                continue;
            }
            if (asset.contains("browser_download_url") &&
                asset["browser_download_url"].is_string()) {
                info.setup_url =
                    asset["browser_download_url"].get<std::string>();
            }
        }
    }
    return info;
}

std::string installer_batch(const std::string& setup_path,
                            const std::string& app_exe, unsigned long pid)
{
    assert(!setup_path.empty() && "nothing to install");
    assert(!app_exe.empty() && "nothing to relaunch");
    assert(pid != 0 && "a pid of 0 would match nothing and never wait");

    // Absolute System32 paths for every tool.  A PATH carrying GNU coreutils
    // -- Git for Windows ships one -- shadows find.exe, and GNU find reads
    // /I as a path and fails.  The failure looks like "the app has exited",
    // so the wait is skipped and the install races the running process.
    const std::string sys32 = "%SystemRoot%\\System32";
    const std::string pid_text = std::to_string(pid);

    // PING, not TIMEOUT: this batch runs with no console attached, and
    // timeout exits immediately with "Input redirection is not supported",
    // collapsing the whole loop.  ping -n N waits N-1 seconds and needs no
    // console.  Roughly 60 seconds in total, then give up and try anyway.
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
    batch += "\"" + setup_path +
             "\" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES\r\n";
    batch += "start \"\" \"" + app_exe + "\"\r\n";
    batch += "del /q \"" + setup_path + "\"\r\n";
    batch += "del /q \"%~f0\"\r\n";
    return batch;
}

}  // namespace mdboss
