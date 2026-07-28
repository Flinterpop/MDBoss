#include "PreviewPane.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>

#include <cassert>
#include <cstdlib>
#include <fstream>

#include <wrl/event.h>

namespace mdboss {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// Schemes a local document may legitimately load.  Everything else is
// refused; see the header for why this exists.
bool is_local_scheme(const std::wstring& uri)
{
    return uri.rfind(L"file:", 0) == 0 || uri.rfind(L"data:", 0) == 0 ||
           uri.rfind(L"about:", 0) == 0 || uri.rfind(L"blob:", 0) == 0;
}

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

// Where the rendered page is staged.  The page is navigated to as a real
// file:// URL rather than pushed through NavigateToString because
// NavigateToString content has an opaque origin, from which Chromium refuses
// to load the file:// images a document references.
wxString preview_dir()
{
    const wxString dir =
        wxFileName::GetTempDir() + wxFileName::GetPathSeparator() + "MDBoss";
    if (!wxDirExists(dir)) {
        wxMkdir(dir);
    }
    return dir;
}

}  // namespace

PreviewPane::PreviewPane(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(*wxWHITE);
    Bind(wxEVT_SIZE, &PreviewPane::on_size, this);
    Bind(wxEVT_IDLE, &PreviewPane::on_idle, this);
    create_webview();
}

void PreviewPane::create_webview()
{
    const HWND parent = static_cast<HWND>(GetHandle());
    if (parent == nullptr) {
        wxLogWarning("Preview: no window handle yet; preview disabled.");
        return;
    }

    const std::wstring user_data =
        widen(std::string(preview_dir().ToUTF8())) + L"\\WebView2";

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, parent](HRESULT result,
                           ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr) {
                    wxLogWarning("Preview: WebView2 environment unavailable; "
                                 "is the Edge runtime installed?");
                    return S_OK;
                }
                environment_ = env;
                return env->CreateCoreWebView2Controller(
                    parent,
                    Callback<
                        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT rc, ICoreWebView2Controller* controller)
                            -> HRESULT {
                            return on_controller_ready(rc, controller);
                        })
                        .Get());
            })
            .Get());
    if (FAILED(hr)) {
        wxLogWarning("Preview: could not start WebView2.");
    }
}

HRESULT PreviewPane::on_controller_ready(HRESULT result,
                                         ICoreWebView2Controller* controller)
{
    if (FAILED(result) || controller == nullptr) {
        wxLogWarning("Preview: WebView2 controller could not be created.");
        return S_OK;
    }
    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);
    if (!webview_) {
        wxLogWarning("Preview: WebView2 created but no core interface.");
        return S_OK;
    }

    // Order matters only in that both must be in place before the first
    // navigation, or the first document's remote references would escape.
    // Be explicit about how put_Bounds() is interpreted.  In DIP mode the
    // rect is multiplied by the rasterization scale, so on a 150% display the
    // view is rendered half again wider than the pane hosting it and the page
    // runs off the edge of the window.  Raw pixels is what GetClientRect()
    // gives us, so ask for raw pixels.
    Microsoft::WRL::ComPtr<ICoreWebView2Controller3> controller3;
    if (SUCCEEDED(controller_.As(&controller3)) && controller3) {
        // Bounds are the raw pixels GetClientRect() gives us, not DIPs.
        controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        controller3->put_ShouldDetectMonitorScaleChanges(TRUE);
    }

    install_network_lock();
    install_scroll_bridge();
    if (std::getenv("MDBOSS_LAYOUT_LOG") != nullptr) {
        install_viewport_probe();
    }
    resize_webview();
    navigate_to_pending();
    return S_OK;
}

// Diagnostic, off unless MDBOSS_LAYOUT_LOG is set.  Asks the page itself how
// wide it thinks it is, instead of inferring the viewport from a screenshot.
void PreviewPane::install_viewport_probe()
{
    EventRegistrationToken token{};
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                webview_->ExecuteScript(
                    L"JSON.stringify({innerWidth: window.innerWidth,"
                    L"clientWidth: document.documentElement.clientWidth,"
                    L"scrollWidth: document.documentElement.scrollWidth,"
                    L"dpr: window.devicePixelRatio,"
                    L"article: (document.querySelector('.markdown-body')||{})"
                    L".getBoundingClientRect?document.querySelector("
                    L"'.markdown-body').getBoundingClientRect().width:-1})",
                    Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                        [this](HRESULT, LPCWSTR result) -> HRESULT {
                            report_viewport(result == nullptr ? L"" : result);
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get(),
        &token);
}

void PreviewPane::report_viewport(const std::wstring& json)
{
    RECT rect{};
    const HWND hwnd = static_cast<HWND>(GetHandle());
    if (hwnd != nullptr) {
        ::GetClientRect(hwnd, &rect);
    }
    if (wxWindow* top = wxGetTopLevelParent(this)) {
        top->SetLabel(wxString::Format("pane %d | page %s",
                                       static_cast<int>(rect.right - rect.left),
                                       wxString(json)));
    }
}

void PreviewPane::install_network_lock()
{
    assert(webview_ && "lock needs a live WebView2");

    EventRegistrationToken token{};
    const HRESULT hr = webview_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2WebResourceRequestedEventArgs* args)
                -> HRESULT { return on_resource_requested(args); })
            .Get(),
        &token);

    // The filter is a URI *pattern*, not a glob over the whole string: a bare
    // "*" is accepted and matches nothing, which would leave the lock looking
    // installed while doing nothing at all.
    static const wchar_t* const kPatterns[] = {
        L"*://*/*", L"http://*/*", L"https://*/*",
    };
    bool filtered = true;
    for (const wchar_t* pattern : kPatterns) {
        if (FAILED(webview_->AddWebResourceRequestedFilter(
                pattern, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL))) {
            filtered = false;
        }
    }

    if (FAILED(hr) || !filtered) {
        // Loud on purpose: this is an export-control control, not a nicety.
        wxLogError("Preview: the network lock could not be installed. "
                   "Remote content in documents would not be blocked.");
    }
}

HRESULT PreviewPane::on_resource_requested(
    ICoreWebView2WebResourceRequestedEventArgs* args)
{
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

    if (is_local_scheme(uri)) {
        return S_OK;
    }
    if (!environment_) {
        // Cannot answer it, so cannot block it -- say so rather than let a
        // request through while pretending otherwise.
        wxLogError("Preview: blocked request could not be answered.");
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

void PreviewPane::install_scroll_bridge()
{
    assert(webview_ && "bridge needs a live WebView2");
    EventRegistrationToken token{};
    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (args == nullptr || !on_scrolled_) {
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
                    on_scrolled_(_wtof(message.substr(7).c_str()));
                }
                return S_OK;
            })
            .Get(),
        &token);
}

void PreviewPane::show_page(const std::string& html)
{
    pending_html_ = html;
    has_pending_ = true;
    if (webview_) {
        navigate_to_pending();
    }
}

void PreviewPane::navigate_to_pending()
{
    if (!webview_ || !has_pending_) {
        return;
    }
    // A fresh filename each time: navigating to the same file:// URL would be
    // treated as a no-op reload and the preview would appear to freeze.
    ++revision_;
    const wxString file =
        preview_dir() + wxFileName::GetPathSeparator() +
        wxString::Format("preview-%llu.html", revision_);

    {
        std::ofstream stream(file.ToStdWstring(),
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            wxLogWarning("Preview: could not stage the rendered page.");
            return;
        }
        stream << pending_html_;
    }
    has_pending_ = false;

    wxString url = file;
    url.Replace("\\", "/");
    webview_->Navigate(widen(std::string(("file:///" + url).ToUTF8())).c_str());

    // Keep the staging directory from growing without bound across a long
    // session; the previous page is no longer needed once the next is staged.
    if (revision_ > 1) {
        const wxString stale =
            preview_dir() + wxFileName::GetPathSeparator() +
            wxString::Format("preview-%llu.html", revision_ - 1);
        wxRemoveFile(stale);
    }
}

void PreviewPane::scroll_to(double ratio)
{
    if (!webview_) {
        return;
    }
    const std::wstring script =
        L"(function(){var h=document.documentElement;"
        L"var max=h.scrollHeight-h.clientHeight;"
        L"window.scrollTo(0, max>0 ? max*" +
        std::to_wstring(ratio) + L" : 0);})();";
    webview_->ExecuteScript(script.c_str(), nullptr);
}

void PreviewPane::on_size(wxSizeEvent& event)
{
    resize_webview();
    event.Skip();
}

void PreviewPane::on_idle(wxIdleEvent& event)
{
    // The web view is sized from whatever the panel currently measures, not
    // from whatever the last event claimed.  Size events alone are not
    // enough: a splitter can reposition this panel and change its width
    // without one arriving in the order you would expect, which left the web
    // view wider than its host and the rendered column running off the right
    // edge of the window.  resize_webview() is a no-op unless the size really
    // changed, so this costs a comparison per idle.
    resize_webview();
    event.Skip();
}

void PreviewPane::resize_webview()
{
    if (!controller_) {
        return;
    }
    // Ask the HWND, not wx.  put_Bounds() is a native API expecting client
    // coordinates of the parent window, and wx's logical geometry does not
    // always agree with them -- feeding it GetClientSize() left the rendered
    // page offset from its host and running off the edge of the window.
    // Taking the rect from the same handle WebView2 was parented to removes
    // the mismatch by construction.
    const HWND hwnd = static_cast<HWND>(GetHandle());
    if (hwnd == nullptr) {
        return;
    }
    RECT bounds{};
    if (!::GetClientRect(hwnd, &bounds)) {
        return;
    }
    const wxSize size(bounds.right - bounds.left, bounds.bottom - bounds.top);
    if (size == applied_bounds_) {
        return;
    }
    applied_bounds_ = size;
    // Checked, not fired and forgotten: if this fails the web view silently
    // keeps the size it was created with -- its parent's full extent -- and
    // the page renders wider than the pane that hosts it.
    const HRESULT hr = controller_->put_Bounds(bounds);
    if (FAILED(hr)) {
        wxLogWarning("Preview: could not resize the web view (0x%08lx).",
                     static_cast<unsigned long>(hr));
        applied_bounds_ = wxSize(-1, -1);   // retry on the next idle
        return;
    }
}

}  // namespace mdboss
