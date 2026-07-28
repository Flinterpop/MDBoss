#include "FileAssoc.h"

#include <cassert>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shlobj.h>

#include "PathUtf8.h"

namespace mdboss {
namespace {

// These strings must match app.py exactly or the two builds would fight over
// the same ProgID with different contents.
const char* const kProgId = "MDBoss.Markdown";
const char* const kProgIdLabel = "Markdown Document";
const char* const kDisplayName = "MD Boss";
const char* const kAppName = "MDBoss";
const char* const kCapabilities = "Software\\MDBoss\\Capabilities";
const char* const kDescription =
    "Local Markdown manager, editor, and offline GitHub-style viewer.";

// Must match app.py's MARKDOWN_EXTS exactly; a missing extension here means
// documents the Python app claims are silently not offered by this one.
const char* const kMarkdownExts[] = {".md", ".markdown", ".mdown", ".mkd",
                                     ".mdwn"};

std::wstring widen(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

}  // namespace

RegPlan registration_plan(const std::string& command, const std::string& icon,
                          const std::string& exe_name)
{
    assert(command.find("%1") != std::string::npos &&
           "command must pass the file through as %1");
    assert(!icon.empty() && "icon must be non-empty");
    assert(!exe_name.empty() && "exe_name must be non-empty");

    const std::string progid = std::string("Software\\Classes\\") + kProgId;
    const std::string appkey =
        "Software\\Classes\\Applications\\" + exe_name;

    RegPlan plan;
    plan.values = {
        {progid, "", kProgIdLabel},
        {progid, "FriendlyTypeName", kProgIdLabel},
        {progid + "\\DefaultIcon", "", icon},
        {progid + "\\shell\\open", "FriendlyAppName", kDisplayName},
        {progid + "\\shell\\open\\command", "", command},
        // Applications\<exe> is what populates the "Open with" list.
        {appkey + "\\shell\\open\\command", "", command},
        {appkey, "FriendlyAppName", kDisplayName},
        // Capabilities + RegisteredApplications list us in Default apps.
        {kCapabilities, "ApplicationName", kDisplayName},
        {kCapabilities, "ApplicationDescription", kDescription},
        {"Software\\RegisteredApplications", kDisplayName, kCapabilities},
    };
    plan.shared_values = {
        {"Software\\RegisteredApplications", kDisplayName},
    };
    for (const char* ext : kMarkdownExts) {
        plan.values.push_back(
            {std::string("Software\\Classes\\") + ext + "\\OpenWithProgids",
             kProgId, ""});
        plan.values.push_back({appkey + "\\SupportedTypes", ext, ""});
        plan.values.push_back(
            {std::string(kCapabilities) + "\\FileAssociations", ext, kProgId});
        plan.shared_values.push_back(
            {std::string("Software\\Classes\\") + ext + "\\OpenWithProgids",
             kProgId});
    }
    plan.owned_keys = {
        progid + "\\shell\\open\\command",
        progid + "\\shell\\open",
        progid + "\\shell",
        progid + "\\DefaultIcon",
        progid,
        appkey + "\\shell\\open\\command",
        appkey + "\\shell\\open",
        appkey + "\\shell",
        appkey + "\\SupportedTypes",
        appkey,
        std::string(kCapabilities) + "\\FileAssociations",
        kCapabilities,
        std::string("Software\\") + kAppName,
    };
    return plan;
}

std::string handler_command()
{
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer, MAX_PATH * 2);
    if (length == 0) {
        return {};
    }
    return "\"" + narrow(std::wstring(buffer, length)) + "\" \"%1\"";
}

RegPlan current_registration_plan()
{
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH * 2);
    const std::string exe = narrow(std::wstring(buffer, length));
    const std::string exe_name = path_to_utf8(path_from_utf8(exe).filename());
    // ",0" selects the first icon resource in the executable.
    return registration_plan(handler_command(), exe + ",0", exe_name);
}

bool apply_registration(const RegPlan& plan)
{
    assert(!plan.values.empty() && "plan must have values to write");
    bool ok = true;
    for (const RegValue& value : plan.values) {
        HKEY key = nullptr;
        const LSTATUS created = RegCreateKeyExW(
            HKEY_CURRENT_USER, widen(value.key).c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (created != ERROR_SUCCESS) {
            ok = false;
            continue;
        }
        const std::wstring data = widen(value.data);
        const LSTATUS set = RegSetValueExW(
            key, value.name.empty() ? nullptr : widen(value.name).c_str(), 0,
            REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
            static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
        if (set != ERROR_SUCCESS) {
            ok = false;
        }
        RegCloseKey(key);
    }
    return ok;
}

void remove_registration(const RegPlan& plan)
{
    // Shared keys give up only our value; anything already gone is fine.
    for (const RegSharedValue& shared : plan.shared_values) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, widen(shared.key).c_str(), 0,
                          KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, widen(shared.name).c_str());
            RegCloseKey(key);
        }
    }
    // Deepest first, so a delete never hits a key that still has children.
    for (const std::string& key : plan.owned_keys) {
        RegDeleteKeyW(HKEY_CURRENT_USER, widen(key).c_str());
    }
}

bool is_registered(const std::string& command)
{
    assert(!command.empty() && "command must be non-empty");
    const std::string key =
        std::string("Software\\Classes\\") + kProgId + "\\shell\\open\\command";

    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, widen(key).c_str(), 0, KEY_QUERY_VALUE,
                      &handle) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[2048]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(
        handle, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(buffer),
        &size);
    RegCloseKey(handle);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }
    return narrow(std::wstring(buffer)) == command;
}

void notify_assoc_changed()
{
    // Cosmetic only: a re-login also refreshes the association table.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}  // namespace mdboss
