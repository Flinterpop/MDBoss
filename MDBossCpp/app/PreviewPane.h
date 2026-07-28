// The rendered-Markdown preview: a WebView2 we create and own, hosted on a
// plain wxPanel.
//
// Deliberately not wxWebView.  The M0 spike established that wxWebView's Edge
// wrapper never delivers WebResourceRequested -- add_WebResourceRequested
// returns S_OK and the handler is then never invoked, for any request, at any
// registration time -- which silently defeats the ITAR network lock.  The same
// code against a WebView2 we create ourselves blocks correctly.  See
// MDBossCpp/spike for the three harnesses that pin that down.
//
// ITAR: every scheme except file/data/about/blob is answered with an empty
// 403, so a stray remote <img> or <script> in a document can never reach the
// network.  This is the WebView2 counterpart of the Python app's
// QWebEngineUrlRequestInterceptor.

#ifndef MDBOSS_APP_PREVIEW_PANE_H
#define MDBOSS_APP_PREVIEW_PANE_H

#include <wx/panel.h>

#include <atomic>
#include <functional>
#include <string>

#include <wrl/client.h>
#include <WebView2.h>

namespace mdboss {

class PreviewPane : public wxPanel {
public:
    explicit PreviewPane(wxWindow* parent);

    // Render `html` (a complete page from mdrender::render_document).  Safe to
    // call before the WebView2 finishes initialising: the last page given is
    // held and shown once it is ready.
    void show_page(const std::string& html);

    // Scroll the preview to `ratio` of its scrollable height, 0..1.
    void scroll_to(double ratio);

    // Scroll to a heading's anchor.  The slug comes from the same pass that
    // produced the id in the HTML, so the two always agree.
    void scroll_to_anchor(const std::string& slug);

    // Called on the UI thread whenever the user scrolls the preview.
    void set_on_scrolled(std::function<void(double)> handler)
    {
        on_scrolled_ = std::move(handler);
    }

    // False until the WebView2 is live; the caller can show a placeholder.
    bool ready() const { return webview_ != nullptr; }

    // How many non-local requests the lock has refused this session.  Exposed
    // so the app can surface it rather than block silently.
    int blocked_requests() const { return blocked_.load(); }

private:
    void create_webview();
    HRESULT on_controller_ready(HRESULT result,
                                ICoreWebView2Controller* controller);
    void install_network_lock();
    void install_scroll_bridge();
    void install_viewport_probe();
    void report_viewport(const std::wstring& json);
    HRESULT on_resource_requested(
        ICoreWebView2WebResourceRequestedEventArgs* args);
    void on_size(wxSizeEvent& event);
    void on_idle(wxIdleEvent& event);
    void resize_webview();
    void navigate_to_pending();

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;

    std::function<void(double)> on_scrolled_;
    std::string pending_html_;
    // The size the web view was last given, so resizing is idempotent and
    // can safely be re-checked on idle.
    wxSize applied_bounds_{-1, -1};
    bool has_pending_ = false;
    unsigned long long revision_ = 0;
    std::atomic<int> blocked_{0};
};

}  // namespace mdboss

#endif  // MDBOSS_APP_PREVIEW_PANE_H
