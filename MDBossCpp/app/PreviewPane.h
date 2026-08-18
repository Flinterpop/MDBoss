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

    // Called when a link to a local MARKDOWN document is clicked, with its
    // path.  The navigation is cancelled first, so the preview never leaves
    // the page it is showing.
    //
    // Markdown only, and it matters: a file: URL is never handed to
    // ShellExecute -- that would LAUNCH it, and a document is untrusted input
    // -- so a link to an .exe or a .bat simply does nothing, exactly as
    // before.  What this adds is one narrow, safe case: opening another
    // document in MD Boss, which is what a link between notes means.
    void set_on_open_document(std::function<void(const std::string&)> handler)
    {
        on_open_document_ = std::move(handler);
    }

    // Write the page currently shown to a PDF at `path`.
    //
    // WebView2's own print pipeline does this, which is what makes the export
    // worth having: it is Chromium laying out the very page on screen, so the
    // PDF carries the GitHub styling, the fonts, the mermaid diagrams and the
    // KaTeX exactly as rendered -- and it emits real link annotations, so a
    // hyperlink stays blue and stays clickable in the reader.  Re-rendering
    // the Markdown through some second PDF library would agree with the
    // preview only by coincidence, and would not agree for long.
    //
    // Asynchronous: `on_done` runs on the UI thread with an empty string on
    // success, or a sentence describing what failed.  Exporting while the
    // preview is still starting up is refused rather than queued -- an export
    // is something the user asked for now, and silently producing the PDF
    // some seconds later, of a page they may have navigated away from, would
    // be worse than saying no.
    void export_pdf(const std::string& path,
                    std::function<void(std::string)> on_done);

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
    // Clicking a link to somewhere outside the machine hands the URL to the
    // default browser instead of loading it here.
    //
    // This does NOT weaken the network lock, and is worth being precise about:
    // the lock stops the PAGE fetching remote content -- a tracking pixel, a
    // remote script -- which happens with no one's consent.  Following a link
    // is a deliberate click, and it is answered by leaving the app entirely.
    // The preview still never loads a remote byte.
    void install_link_handler();
    // Send `uri` to the shell.  Only http/https/mailto: anything else clicked
    // in a document is refused rather than handed to ShellExecute, which
    // would happily run a local executable given a file: URL.
    void open_externally(const std::wstring& uri);
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
    std::function<void(const std::string&)> on_open_document_;
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
