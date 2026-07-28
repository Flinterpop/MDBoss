// Milestone 0 spike -- throwaway, not part of the shipped app.
//
// Retires the two unknowns that could invalidate the wxWidgets + wxWebViewEdge
// stack choice before any GUI work is committed to:
//
//   1. Bidirectional scroll sync.  app.py drives this with QWebChannel and a
//      ScrollBridge QObject.  wxWebView has no channel; the replacement is
//      AddScriptMessageHandler (preview -> app) plus RunScript (app ->
//      preview).  This checks a value survives a round trip in both
//      directions, because that is what the editor/preview split depends on.
//
//   2. The ITAR network lock.  app.py installs a QWebEngineUrlRequestInterceptor
//      that blocks every scheme but file/data/qrc/about/blob, so a stray remote
//      <img> in a document can never phone home.  wxWebView exposes no portable
//      equivalent, so this reaches the native ICoreWebView2 and installs a
//      WebResourceRequested filter.  The test is empirical, not a code
//      inspection: a real listening socket on 127.0.0.1 records any connection,
//      and the document points an <img> at it.
//
// Exits 0 only if every check passes; the result lines are printed so the run
// is auditable rather than just a return code.

#include <wx/wx.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/webview.h>
#include <wx/msw/webview_edge.h>

#include <atomic>
#include <string>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wrl/client.h>
#include <wrl/event.h>     // Microsoft::WRL::Callback
#include <WebView2.h>

#pragma comment(lib, "Ws2_32.lib")

namespace {

// ------------------------------------------------------- egress detector --

// A real listening socket, so "was the request blocked?" is answered by
// whether a TCP connection ever arrived -- not by trusting our own filter.
class EgressProbe {
public:
    bool start()
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET) {
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;   // let the OS choose
        if (bind(listener_, reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) != 0) {
            return false;
        }
        int len = sizeof(addr);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&addr),
                        &len) != 0) {
            return false;
        }
        port_ = ntohs(addr.sin_port);
        if (listen(listener_, 8) != 0) {
            return false;
        }
        thread_ = std::thread([this] { accept_loop(); });
        return true;
    }

    void stop()
    {
        running_ = false;
        if (listener_ != INVALID_SOCKET) {
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        WSACleanup();
    }

    unsigned short port() const { return port_; }
    bool was_contacted() const { return contacted_.load(); }

private:
    void accept_loop()
    {
        while (running_.load()) {
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                return;   // closed by stop(), or a real error
            }
            contacted_ = true;
            closesocket(client);
        }
    }

    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{true};
    std::atomic<bool> contacted_{false};
};

// ------------------------------------------------------------ the frame --

// A wxTimer constructed with an owner posts its events to that owner, not to
// itself, so the two timers need distinct ids and must be bound on the frame.
constexpr int kStepTimerId = 1001;
constexpr int kWatchdogTimerId = 1002;

class SpikeFrame : public wxFrame {
public:
    SpikeFrame(unsigned short probe_port, bool use_file_url)
        : wxFrame(nullptr, wxID_ANY, "MDBoss spike", wxDefaultPosition,
                  wxSize(900, 600)),
          probe_port_(probe_port),
          use_file_url_(use_file_url)
    {
        wxPrintf("--- loading via %s ---\n",
                 use_file_url_ ? "LoadURL(file://)" : "SetPage/NavigateToString");
        fflush(stdout);
        view_ = wxWebView::New(this, wxID_ANY, "about:blank",
                               wxDefaultPosition, wxDefaultSize,
                               wxWebViewBackendEdge);
        if (view_ == nullptr) {
            report("webview-new", false, "wxWebView::New returned null");
            finish();
            return;
        }

        Bind(wxEVT_WEBVIEW_CREATED, &SpikeFrame::on_created, this);
        Bind(wxEVT_WEBVIEW_LOADED, &SpikeFrame::on_loaded, this);
        Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
             &SpikeFrame::on_script_message, this);
        Bind(wxEVT_TIMER, &SpikeFrame::on_tick, this, kStepTimerId);
        Bind(wxEVT_TIMER, &SpikeFrame::on_watchdog, this, kWatchdogTimerId);

        // The handler must exist before the page runs, or the injected
        // listener has nothing to post to.
        const bool handler = view_->AddScriptMessageHandler("mdboss");
        report("script-message-handler", handler,
               handler ? "AddScriptMessageHandler(\"mdboss\") accepted"
                       : "backend rejected the handler");

        // Nothing else can happen yet.  The Edge backend builds its
        // WebView2 asynchronously: GetNativeBackend() returns null until
        // wxEVT_WEBVIEW_CREATED fires, so both the network lock and the first
        // SetPage have to wait for it.  This is the single most important
        // finding of the spike -- installing the ITAR lock in a constructor,
        // the way the Qt interceptor is installed today, silently does
        // nothing.
        watchdog_.StartOnce(30000);
    }

private:
    // ---------------------------------------------------- network lock --

    void install_network_lock()
    {
        void* native = view_->GetNativeBackend();
        if (native == nullptr) {
            report("native-backend", false, "GetNativeBackend() returned null");
            return;
        }

        // wx documents this as an ICoreWebView2*, but the spike's whole job is
        // to stop us relying on that, so probe both shapes.
        Microsoft::WRL::ComPtr<ICoreWebView2> core;
        Microsoft::WRL::ComPtr<IUnknown> unknown(
            static_cast<IUnknown*>(native));
        std::string shape = "ICoreWebView2";
        if (FAILED(unknown.As(&core)) || !core) {
            Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
            if (SUCCEEDED(unknown.As(&controller)) && controller) {
                controller->get_CoreWebView2(&core);
                shape = "ICoreWebView2Controller -> get_CoreWebView2";
            }
        }
        if (!core) {
            report("native-backend", false,
                   "native backend is neither ICoreWebView2 nor a controller");
            return;
        }
        report("native-backend", true, "reached " + shape);
        core_ = core;

        // Blocking a request means answering it, and building a response needs
        // the environment.  Resolve it up front and say so: if this is only
        // discovered inside the handler, a failure there looks like a blocked
        // request while the fetch actually proceeds.
        Microsoft::WRL::ComPtr<ICoreWebView2_2> core2;
        if (SUCCEEDED(core.As(&core2)) && core2 &&
            SUCCEEDED(core2->get_Environment(&environment_)) && environment_) {
            report("webview2-environment", true,
                   "ICoreWebView2_2::get_Environment succeeded");
        } else {
            report("webview2-environment", false,
                   "no environment: requests cannot be answered, so blocking "
                   "would silently fall through");
        }

        EventRegistrationToken token{};
        const HRESULT hr = core->add_WebResourceRequested(
            Microsoft::WRL::Callback<
                ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2*,
                       ICoreWebView2WebResourceRequestedEventArgs* args)
                    -> HRESULT {
                    return on_resource_requested(args);
                })
                .Get(),
            &token);
        report("network-lock-handler", SUCCEEDED(hr),
               SUCCEEDED(hr) ? "add_WebResourceRequested accepted"
                             : "add_WebResourceRequested failed");

        // The filter is a URI *pattern*, not a glob over the whole string: wx
        // itself registers "*://host/*" (webview_edge.cpp:909).  A bare "*"
        // is accepted -- it returns S_OK -- but matches nothing, so the
        // handler is never raised and the lock silently does nothing.  Register
        // several explicit patterns instead.
        static const wchar_t* const kPatterns[] = {
            L"*://*/*",        // every scheme with an authority
            L"http://*/*",
            L"https://*/*",
        };
        bool all_ok = true;
        for (const wchar_t* pattern : kPatterns) {
            if (FAILED(core->AddWebResourceRequestedFilter(
                    pattern, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL))) {
                all_ok = false;
            }
        }
        report("network-lock-filter", all_ok,
               all_ok ? "registered URI-pattern filters (*://*/* and http(s))"
                      : "AddWebResourceRequestedFilter rejected a pattern");
    }

    // Block every scheme except the ones a local document legitimately needs.
    HRESULT on_resource_requested(
        ICoreWebView2WebResourceRequestedEventArgs* args)
    {
        // Counted first: the early returns below would otherwise make an
        // invoked-but-bailing handler indistinguishable from one that never
        // ran, which is exactly the question being asked.
        invoked_count_.fetch_add(1);

        if (args == nullptr) {
            return S_OK;
        }
        Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
        if (FAILED(args->get_Request(&request)) || !request) {
            return S_OK;
        }
        LPWSTR raw = nullptr;
        if (FAILED(request->get_Uri(&raw)) || raw == nullptr) {
            return S_OK;
        }
        const std::wstring uri(raw);
        CoTaskMemFree(raw);

        total_count_.fetch_add(1);
        if (seen_uris_.size() < 12) {
            seen_uris_.push_back(wxString(uri).ToStdString());
        }

        const bool allowed = uri.rfind(L"file:", 0) == 0 ||
                             uri.rfind(L"data:", 0) == 0 ||
                             uri.rfind(L"about:", 0) == 0 ||
                             uri.rfind(L"blob:", 0) == 0;
        if (allowed) {
            return S_OK;
        }

        seen_count_.fetch_add(1);
        // An empty 403 response, so the page sees a failure rather than a hang.
        if (!environment_) {
            return S_OK;   // counted as seen, but NOT blocked
        }
        Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
        if (FAILED(environment_->CreateWebResourceResponse(
                nullptr, 403, L"Blocked", L"", &response)) ||
            !response) {
            return S_OK;
        }
        if (SUCCEEDED(args->put_Response(response.Get()))) {
            blocked_count_.fetch_add(1);
        }
        return S_OK;
    }

    // ------------------------------------------------------ scroll sync --

    wxString document() const
    {
        // A remote <img> pointed at the local probe socket: if the lock leaks,
        // a real TCP connection arrives and the probe records it.
        return wxString::Format(
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<title>spike</title></head><body>"
            "<img src=\"http://127.0.0.1:%u/probe.png\" alt=\"probe\">"
            "<div style=\"height:4000px\">tall</div>"
            "<script>"
            "window.addEventListener('scroll', function() {"
            "  var h = document.documentElement.scrollHeight - "
            "window.innerHeight;"
            "  var r = h > 0 ? window.scrollY / h : 0;"
            "  window.mdboss.postMessage('scroll:' + r.toFixed(4));"
            "});"
            "</script></body></html>",
            static_cast<unsigned>(probe_port_));
    }

    // Two ways to get the same HTML in front of the engine.  app.py uses
    // setHtml(), whose Edge equivalent is NavigateToString, so that is the
    // default; the file:// path is here to tell "the lock does not work" apart
    // from "the lock does not work for NavigateToString content".
    void load_document()
    {
        if (!use_file_url_) {
            view_->SetPage(document(), "file:///C:/source/MDBoss/");
            return;
        }
        const wxString path =
            wxFileName::GetTempDir() + "\\mdboss_spike_probe.html";
        wxFile file;
        if (!file.Create(path, true) || !file.Write(document())) {
            report("temp-document", false, "could not write " +
                                               path.ToStdString());
            return;
        }
        file.Close();
        view_->LoadURL("file:///" + path);
    }

    void on_created(wxWebViewEvent&)
    {
        created_ = true;
        report("webview-created", true,
               "wxEVT_WEBVIEW_CREATED fired; native backend now reachable");
        install_network_lock();
        load_document();
        timer_.Start(600);
    }

    void on_loaded(wxWebViewEvent&)
    {
        loaded_ = true;
    }

    // Backstop: never let an unattended run hang.
    void on_watchdog(wxTimerEvent&)
    {
        if (!created_) {
            report("webview-created", false,
                   "wxEVT_WEBVIEW_CREATED never fired within 30s");
        }
        report("watchdog", false, "spike did not finish on its own");
        finish();
    }

    void on_script_message(wxWebViewEvent& event)
    {
        const wxString message = event.GetString();
        if (!message.StartsWith("scroll:")) {
            return;
        }
        double ratio = 0.0;
        if (message.Mid(7).ToDouble(&ratio)) {
            last_ratio_ = ratio;
            ++message_count_;
        }
    }

    // ----------------------------------------------------- test driver --

    void on_tick(wxTimerEvent&)
    {
        ++step_;
        switch (step_) {
        case 1: {
            // Is the ICoreWebView2 we registered on still the live one?  wx
            // can rebuild the Edge controller, which would silently discard
            // the handler and leave the lock looking installed.
            void* native = view_->GetNativeBackend();
            const bool same = native == static_cast<void*>(core_.Get());
            report("native-backend stable", same,
                   same ? "same ICoreWebView2 as at registration time"
                        : "backend pointer CHANGED since registration");
            // A dead port: refused instantly, so it cannot reach the probe.
            // If the handler does not even see a top-level navigation, the
            // registration is not live.
            view_->LoadURL("http://127.0.0.1:1/registration-probe");
            break;
        }
        case 2: {
            const bool fires = invoked_count_.load() > 0;
            report("handler receives requests", fires,
                   fires ? "WebResourceRequested fired " +
                               std::to_string(invoked_count_.load()) +
                               " time(s); " +
                               std::to_string(total_count_.load()) +
                               " yielded a URI"
                         : "no WebResourceRequested callback at all");
            load_document();
            break;
        }
        case 3:
        case 4:
            break;   // let the document load
        case 5:
            // app -> preview: drive the view from the host side.
            view_->RunScriptAsync(
                "window.scrollTo(0, (document.documentElement.scrollHeight - "
                "window.innerHeight) * 0.5);");
            break;
        case 6: {
            // preview -> app: the injected listener should have posted a
            // ratio back through the script-message handler.
            const bool got = message_count_ > 0;
            report("scroll-sync preview->app", got,
                   got ? "received ratio " + std::to_string(last_ratio_)
                       : "no script message arrived");
            const bool near_half =
                got && last_ratio_ > 0.45 && last_ratio_ < 0.55;
            report("scroll-sync app->preview", near_half,
                   near_half ? "host-driven scroll landed at the right offset"
                             : "ratio did not match the requested position");
            break;
        }
        case 7: {
            // The remote <img> has had several seconds to resolve by now.
            const bool clean = !probe_contacted_();
            const std::string counts =
                " (" + std::to_string(total_count_.load()) +
                " request(s) reached the handler, " +
                std::to_string(seen_count_.load()) + " non-local, " +
                std::to_string(blocked_count_.load()) + " answered with 403)";
            report("itar network lock", clean,
                   (clean ? "no TCP connection reached the probe"
                          : "LEAK: the probe socket was contacted") +
                       counts);
            for (const std::string& uri : seen_uris_) {
                wxPrintf("         saw: %s\n", uri.c_str());
            }
            fflush(stdout);
            break;
        }
        default:
            finish();
            break;
        }
    }

    void finish()
    {
        timer_.Stop();
        watchdog_.Stop();
        Close(true);
    }

    void report(const std::string& name, bool ok, const std::string& detail)
    {
        if (!ok) {
            failures_ = true;
        }
        wxPrintf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", name.c_str(),
                 detail.c_str());
        fflush(stdout);
    }

public:
    static bool failures_;
    static std::function<bool()> probe_contacted_;

private:
    wxWebView* view_ = nullptr;
    wxTimer timer_{this, kStepTimerId};
    wxTimer watchdog_{this, kWatchdogTimerId};
    unsigned short probe_port_ = 0;
    bool use_file_url_ = false;
    bool created_ = false;
    bool loaded_ = false;
    int step_ = 0;
    int message_count_ = 0;
    double last_ratio_ = 0.0;
    std::atomic<int> invoked_count_{0};
    std::atomic<int> total_count_{0};
    std::atomic<int> seen_count_{0};
    std::atomic<int> blocked_count_{0};
    std::vector<std::string> seen_uris_;
    Microsoft::WRL::ComPtr<ICoreWebView2> core_;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
};

bool SpikeFrame::failures_ = false;
std::function<bool()> SpikeFrame::probe_contacted_ = [] { return false; };

EgressProbe g_probe;

class SpikeApp : public wxApp {
public:
    bool OnInit() override
    {
        if (!g_probe.start()) {
            wxPrintf("[FAIL] %-28s could not open the probe socket\n",
                     "egress-probe");
            return false;
        }
        SpikeFrame::probe_contacted_ = [] { return g_probe.was_contacted(); };
        bool use_file_url = false;
        for (int i = 1; i < argc; ++i) {
            if (wxString(argv[i]) == "--file") {
                use_file_url = true;
            }
        }
        auto* frame = new SpikeFrame(g_probe.port(), use_file_url);
        frame->Show(true);
        return true;
    }

    int OnExit() override
    {
        g_probe.stop();
        return 0;
    }
};

}  // namespace

wxIMPLEMENT_APP_NO_MAIN(SpikeApp);

int main(int argc, char** argv)
{
    const int rc = wxEntry(argc, argv);
    if (rc != 0) {
        return rc;
    }
    wxPrintf("\n%s\n", SpikeFrame::failures_ ? "SPIKE FAILED"
                                             : "SPIKE PASSED");
    return SpikeFrame::failures_ ? 1 : 0;
}
