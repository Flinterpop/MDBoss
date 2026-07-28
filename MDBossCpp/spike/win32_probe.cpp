// Milestone 0 discriminator -- bare Win32 + WebView2, no wxWidgets.
//
// The wx harness (main.cpp) showed add_WebResourceRequested returning S_OK and
// then never being invoked, so a remote <img> reached the network.  That left
// one question open: is the fault wxWidgets' wrapper, or this code's use of
// WebView2?
//
// This harness answers it by doing the same thing with nothing between it and
// the API -- it creates the environment and controller itself, installs the
// same handler with the same filters, loads the same document through
// NavigateToString, and uses the same EgressProbe.  The only variable removed
// is wx.
//
//   * If the probe is NOT contacted here, the approach is sound and wx's
//     wrapper is at fault -- the fix is to own the WebView2 creation.
//   * If the probe IS contacted here too, the approach itself is wrong and
//     the WebView2-based lock cannot be trusted as written.

// Winsock 1 ships inside <windows.h> and collides with <winsock2.h>, which
// EgressProbe.h needs.  WIN32_LEAN_AND_MEAN keeps the old header out.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <wrl/client.h>
#include <wrl/event.h>
#include <WebView2.h>

#include "EgressProbe.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT_PTR kEndTimerId = 1;
constexpr UINT kRunMilliseconds = 9000;

EgressProbe g_probe;
bool g_failures = false;

struct State {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    ComPtr<ICoreWebView2Environment> environment;
    std::atomic<int> invoked{0};
    std::atomic<int> non_local{0};
    std::atomic<int> blocked{0};
    std::vector<std::string> seen;
    bool navigated = false;
};

State g_state;

void report(const char* name, bool ok, const std::string& detail)
{
    if (!ok) {
        g_failures = true;
    }
    std::printf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", name,
                detail.c_str());
    std::fflush(stdout);
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

// Byte-for-byte the same document the wx harness loads.
std::wstring document(unsigned short port)
{
    wchar_t buffer[1024];
    swprintf(buffer, 1024,
             L"<!doctype html><html><head><meta charset=\"utf-8\">"
             L"<title>spike</title></head><body>"
             L"<img src=\"http://127.0.0.1:%u/probe.png\" alt=\"probe\">"
             L"<div style=\"height:4000px\">tall</div>"
             L"</body></html>",
             static_cast<unsigned>(port));
    return buffer;
}

HRESULT on_resource_requested(ICoreWebView2WebResourceRequestedEventArgs* args)
{
    g_state.invoked.fetch_add(1);
    if (args == nullptr) {
        return S_OK;
    }
    ComPtr<ICoreWebView2WebResourceRequest> request;
    if (FAILED(args->get_Request(&request)) || !request) {
        return S_OK;
    }
    LPWSTR raw = nullptr;
    if (FAILED(request->get_Uri(&raw)) || raw == nullptr) {
        return S_OK;
    }
    const std::wstring uri(raw);
    CoTaskMemFree(raw);

    if (g_state.seen.size() < 12) {
        g_state.seen.push_back(narrow(uri));
    }

    const bool allowed = uri.rfind(L"file:", 0) == 0 ||
                         uri.rfind(L"data:", 0) == 0 ||
                         uri.rfind(L"about:", 0) == 0 ||
                         uri.rfind(L"blob:", 0) == 0;
    if (allowed) {
        return S_OK;
    }

    g_state.non_local.fetch_add(1);
    if (!g_state.environment) {
        return S_OK;
    }
    ComPtr<ICoreWebView2WebResourceResponse> response;
    if (FAILED(g_state.environment->CreateWebResourceResponse(
            nullptr, 403, L"Blocked", L"", &response)) ||
        !response) {
        return S_OK;
    }
    if (SUCCEEDED(args->put_Response(response.Get()))) {
        g_state.blocked.fetch_add(1);
    }
    return S_OK;
}

void install_lock_and_navigate(HWND hwnd)
{
    if (!g_state.webview) {
        return;
    }
    EventRegistrationToken token{};
    const HRESULT hr = g_state.webview->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [](ICoreWebView2*,
               ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                return on_resource_requested(args);
            })
            .Get(),
        &token);
    report("network-lock-handler", SUCCEEDED(hr),
           SUCCEEDED(hr) ? "add_WebResourceRequested accepted"
                         : "add_WebResourceRequested failed");

    static const wchar_t* const kPatterns[] = {
        L"*://*/*", L"http://*/*", L"https://*/*",
    };
    bool all_ok = true;
    for (const wchar_t* pattern : kPatterns) {
        if (FAILED(g_state.webview->AddWebResourceRequestedFilter(
                pattern, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL))) {
            all_ok = false;
        }
    }
    report("network-lock-filter", all_ok,
           all_ok ? "registered the same URI-pattern filters as the wx run"
                  : "AddWebResourceRequestedFilter rejected a pattern");

    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    g_state.controller->put_Bounds(bounds);

    const std::wstring html = document(g_probe.port());
    const HRESULT nav = g_state.webview->NavigateToString(html.c_str());
    g_state.navigated = SUCCEEDED(nav);
    report("navigate", g_state.navigated,
           g_state.navigated ? "NavigateToString accepted"
                             : "NavigateToString failed");
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_TIMER && wparam == kEndTimerId) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

}  // namespace

int main()
{
    if (!g_probe.start()) {
        report("egress-probe", false, "could not open the probe socket");
        return 1;
    }
    std::printf("--- bare Win32 + WebView2, no wxWidgets ---\n");
    std::fflush(stdout);

    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co)) {
        report("co-initialize", false, "CoInitializeEx failed");
        g_probe.stop();
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MDBossSpikeWin32";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MDBoss spike (Win32)",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 900, 600, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        report("create-window", false, "CreateWindowExW failed");
        g_probe.stop();
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring user_data = std::wstring(temp) + L"mdboss_spike_win32";

    const HRESULT env_hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result,
                   ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr) {
                    report("environment", false,
                           "CreateCoreWebView2Environment failed");
                    PostQuitMessage(0);
                    return S_OK;
                }
                g_state.environment = env;
                report("environment", true,
                       "created our own ICoreWebView2Environment");

                return env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<
                        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT rc,
                               ICoreWebView2Controller* controller)
                            -> HRESULT {
                            if (FAILED(rc) || controller == nullptr) {
                                report("controller", false,
                                       "CreateCoreWebView2Controller failed");
                                PostQuitMessage(0);
                                return S_OK;
                            }
                            g_state.controller = controller;
                            controller->get_CoreWebView2(&g_state.webview);
                            report("controller", g_state.webview != nullptr,
                                   g_state.webview
                                       ? "got ICoreWebView2 from our own "
                                         "controller"
                                       : "get_CoreWebView2 returned null");
                            install_lock_and_navigate(hwnd);
                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    if (FAILED(env_hr)) {
        report("environment", false,
               "CreateCoreWebView2EnvironmentWithOptions failed -- is the "
               "Evergreen runtime installed?");
        g_probe.stop();
        return 1;
    }

    SetTimer(hwnd, kEndTimerId, kRunMilliseconds, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    const bool fired = g_state.invoked.load() > 0;
    report("handler receives requests", fired,
           fired ? "WebResourceRequested fired " +
                       std::to_string(g_state.invoked.load()) + " time(s)"
                 : "no WebResourceRequested callback at all");

    const bool clean = !g_probe.was_contacted();
    report("itar network lock", clean,
           (clean ? std::string("no TCP connection reached the probe")
                  : std::string("LEAK: the probe socket was contacted")) +
               " (" + std::to_string(g_state.invoked.load()) +
               " invocation(s), " + std::to_string(g_state.non_local.load()) +
               " non-local, " + std::to_string(g_state.blocked.load()) +
               " answered with 403)");
    for (const std::string& uri : g_state.seen) {
        std::printf("         saw: %s\n", uri.c_str());
    }

    g_state.webview.Reset();
    g_state.controller.Reset();
    g_state.environment.Reset();
    CoUninitialize();
    g_probe.stop();

    std::printf("\n%s\n", g_failures ? "WIN32 SPIKE FAILED"
                                     : "WIN32 SPIKE PASSED");
    return g_failures ? 1 : 0;
}
