// Milestone 0, final step -- the proposed architecture, end to end.
//
// Established by the first two harnesses:
//   * bare Win32 + WebView2 blocks the remote fetch (win32_probe.cpp);
//   * the same code behind wxWebView never receives the event at all
//     (main.cpp), regardless of registration timing, order, filter pattern or
//     load method.
//
// So the fix is to keep wxWidgets for every widget and stop using wxWebView
// for the preview: create the WebView2 ourselves and parent it to a wxWindow's
// HWND.  This harness proves that combination works, because it is what
// Milestone 2 would be built on:
//
//   * the ITAR network lock actually blocks (the empirical probe again), and
//   * scroll sync round-trips using WebView2's own messaging
//     (add_WebMessageReceived + ExecuteScript) rather than wx's wrappers.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <wx/wx.h>

#include <atomic>
#include <string>
#include <vector>

#include <wrl/client.h>
#include <wrl/event.h>
#include <WebView2.h>

#include "EgressProbe.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kStepTimerId = 2001;

EgressProbe g_probe;
bool g_failures = false;

void report(const char* name, bool ok, const std::string& detail)
{
    if (!ok) {
        g_failures = true;
    }
    wxPrintf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    fflush(stdout);
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

class OwnedFrame : public wxFrame {
public:
    explicit OwnedFrame(unsigned short probe_port)
        : wxFrame(nullptr, wxID_ANY, "MDBoss spike (wx + owned WebView2)",
                  wxDefaultPosition, wxSize(900, 600)),
          probe_port_(probe_port)
    {
        wxPrintf("--- wxWidgets frame hosting a WebView2 we create ---\n");
        fflush(stdout);

        // A perfectly ordinary wx layout: the preview is just a wxPanel whose
        // HWND we hand to WebView2.  Everything else in the app stays wx.
        host_ = new wxPanel(this, wxID_ANY);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(host_, 1, wxEXPAND);
        SetSizer(sizer);
        Layout();

        Bind(wxEVT_TIMER, &OwnedFrame::on_tick, this, kStepTimerId);
        Bind(wxEVT_SIZE, &OwnedFrame::on_size, this);

        create_webview();
        timer_.Start(900);
    }

private:
    void create_webview()
    {
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring user_data = std::wstring(temp) + L"mdboss_spike_wxown";

        const HWND parent = static_cast<HWND>(host_->GetHandle());
        if (parent == nullptr) {
            report("host-hwnd", false, "wxPanel has no HWND yet");
            return;
        }
        report("host-hwnd", true, "wxPanel HWND available for WebView2");

        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, user_data.c_str(), nullptr,
            Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, parent](HRESULT rc,
                               ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(rc) || env == nullptr) {
                        report("environment", false,
                               "CreateCoreWebView2Environment failed");
                        return S_OK;
                    }
                    environment_ = env;
                    report("environment", true,
                           "created our own ICoreWebView2Environment");
                    return env->CreateCoreWebView2Controller(
                        parent,
                        Callback<
                            ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT crc,
                                   ICoreWebView2Controller* controller)
                                -> HRESULT {
                                return on_controller(crc, controller);
                            })
                            .Get());
                })
                .Get());
        if (FAILED(hr)) {
            report("environment", false,
                   "CreateCoreWebView2EnvironmentWithOptions failed");
        }
    }

    HRESULT on_controller(HRESULT rc, ICoreWebView2Controller* controller)
    {
        if (FAILED(rc) || controller == nullptr) {
            report("controller", false, "CreateCoreWebView2Controller failed");
            return S_OK;
        }
        controller_ = controller;
        controller_->get_CoreWebView2(&webview_);
        report("controller", webview_ != nullptr,
               webview_ ? "ICoreWebView2 hosted inside the wx panel"
                        : "get_CoreWebView2 returned null");
        if (!webview_) {
            return S_OK;
        }

        install_network_lock();
        install_scroll_bridge();
        resize_webview();

        const std::wstring html = document();
        report("navigate", SUCCEEDED(webview_->NavigateToString(html.c_str())),
               "NavigateToString issued");
        return S_OK;
    }

    void install_network_lock()
    {
        EventRegistrationToken token{};
        const HRESULT hr = webview_->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2*,
                       ICoreWebView2WebResourceRequestedEventArgs* args)
                    -> HRESULT { return on_resource_requested(args); })
                .Get(),
            &token);

        static const wchar_t* const kPatterns[] = {
            L"*://*/*", L"http://*/*", L"https://*/*",
        };
        bool all_ok = SUCCEEDED(hr);
        for (const wchar_t* pattern : kPatterns) {
            if (FAILED(webview_->AddWebResourceRequestedFilter(
                    pattern, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL))) {
                all_ok = false;
            }
        }
        report("network-lock-install", all_ok,
               all_ok ? "handler and URI-pattern filters registered"
                      : "registration failed");
    }

    HRESULT on_resource_requested(
        ICoreWebView2WebResourceRequestedEventArgs* args)
    {
        invoked_.fetch_add(1);
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
        if (seen_.size() < 12) {
            seen_.push_back(narrow(uri));
        }

        const bool allowed = uri.rfind(L"file:", 0) == 0 ||
                             uri.rfind(L"data:", 0) == 0 ||
                             uri.rfind(L"about:", 0) == 0 ||
                             uri.rfind(L"blob:", 0) == 0;
        if (allowed) {
            return S_OK;
        }
        non_local_.fetch_add(1);
        if (!environment_) {
            return S_OK;
        }
        ComPtr<ICoreWebView2WebResourceResponse> response;
        if (FAILED(environment_->CreateWebResourceResponse(
                nullptr, 403, L"Blocked", L"", &response)) ||
            !response) {
            return S_OK;
        }
        if (SUCCEEDED(args->put_Response(response.Get()))) {
            blocked_.fetch_add(1);
        }
        return S_OK;
    }

    // The QWebChannel/ScrollBridge replacement, using WebView2 directly.
    void install_scroll_bridge()
    {
        EventRegistrationToken token{};
        const HRESULT hr = webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*,
                       ICoreWebView2WebMessageReceivedEventArgs* args)
                    -> HRESULT {
                    if (args == nullptr) {
                        return S_OK;
                    }
                    LPWSTR raw = nullptr;
                    if (FAILED(args->TryGetWebMessageAsString(&raw)) ||
                        raw == nullptr) {
                        return S_OK;
                    }
                    const std::wstring message(raw);
                    CoTaskMemFree(raw);
                    if (message.rfind(L"scroll:", 0) == 0) {
                        last_ratio_ = _wtof(message.substr(7).c_str());
                        ++messages_;
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        report("scroll-bridge-install", SUCCEEDED(hr),
               SUCCEEDED(hr) ? "add_WebMessageReceived registered"
                             : "add_WebMessageReceived failed");
    }

    std::wstring document() const
    {
        wchar_t buffer[1400];
        swprintf(buffer, 1400,
                 L"<!doctype html><html><head><meta charset=\"utf-8\">"
                 L"</head><body>"
                 L"<img src=\"http://127.0.0.1:%u/probe.png\" alt=\"probe\">"
                 L"<div style=\"height:4000px\">tall</div>"
                 L"<script>"
                 L"window.addEventListener('scroll', function() {"
                 L"  var h = document.documentElement.scrollHeight - "
                 L"window.innerHeight;"
                 L"  var r = h > 0 ? window.scrollY / h : 0;"
                 L"  window.chrome.webview.postMessage('scroll:' + "
                 L"r.toFixed(4));"
                 L"});"
                 L"</script></body></html>",
                 static_cast<unsigned>(probe_port_));
        return buffer;
    }

    void on_size(wxSizeEvent& event)
    {
        resize_webview();
        event.Skip();
    }

    void resize_webview()
    {
        if (!controller_ || host_ == nullptr) {
            return;
        }
        const wxSize size = host_->GetClientSize();
        RECT bounds{0, 0, size.GetWidth(), size.GetHeight()};
        controller_->put_Bounds(bounds);
    }

    void on_tick(wxTimerEvent&)
    {
        ++step_;
        switch (step_) {
        case 1:
        case 2:
            break;
        case 3:
            if (webview_) {
                webview_->ExecuteScript(
                    L"window.scrollTo(0, "
                    L"(document.documentElement.scrollHeight - "
                    L"window.innerHeight) * 0.5);",
                    nullptr);
            }
            break;
        case 4: {
            const bool got = messages_ > 0;
            report("scroll-sync preview->app", got,
                   got ? "received ratio " + std::to_string(last_ratio_)
                       : "no web message arrived");
            const bool near_half =
                got && last_ratio_ > 0.45 && last_ratio_ < 0.55;
            report("scroll-sync app->preview", near_half,
                   near_half ? "host-driven scroll landed at the right offset"
                             : "ratio did not match the requested position");
            break;
        }
        case 5: {
            const bool clean = !g_probe.was_contacted();
            report("itar network lock", clean,
                   (clean ? std::string("no TCP connection reached the probe")
                          : std::string("LEAK: the probe socket was "
                                        "contacted")) +
                       " (" + std::to_string(invoked_.load()) +
                       " invocation(s), " + std::to_string(non_local_.load()) +
                       " non-local, " + std::to_string(blocked_.load()) +
                       " answered with 403)");
            for (const std::string& uri : seen_) {
                wxPrintf("         saw: %s\n", uri.c_str());
            }
            fflush(stdout);
            break;
        }
        default:
            timer_.Stop();
            Close(true);
            break;
        }
    }

    wxPanel* host_ = nullptr;
    wxTimer timer_{this, kStepTimerId};
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    unsigned short probe_port_ = 0;
    int step_ = 0;
    int messages_ = 0;
    double last_ratio_ = 0.0;
    std::atomic<int> invoked_{0};
    std::atomic<int> non_local_{0};
    std::atomic<int> blocked_{0};
    std::vector<std::string> seen_;
};

class OwnedApp : public wxApp {
public:
    bool OnInit() override
    {
        if (!g_probe.start()) {
            wxPrintf("[FAIL] egress-probe could not open the probe socket\n");
            return false;
        }
        (new OwnedFrame(g_probe.port()))->Show(true);
        return true;
    }

    int OnExit() override
    {
        g_probe.stop();
        return 0;
    }
};

}  // namespace

wxIMPLEMENT_APP_NO_MAIN(OwnedApp);

int main(int argc, char** argv)
{
    const int rc = wxEntry(argc, argv);
    if (rc != 0) {
        return rc;
    }
    wxPrintf("\n%s\n", g_failures ? "WX+OWNED SPIKE FAILED"
                                  : "WX+OWNED SPIKE PASSED");
    return g_failures ? 1 : 0;
}
