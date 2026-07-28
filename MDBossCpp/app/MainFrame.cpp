#include "MainFrame.h"

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textfile.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "mdrender/MdRender.h"

namespace mdboss {
namespace {

// Matches app.py's PREVIEW_DEBOUNCE_MS: long enough that typing does not
// re-render on every keystroke, short enough to feel live.
constexpr int kRenderDebounceMs = 300;

constexpr int kIdToggleFrontMatter = wxID_HIGHEST + 1;

const char* const kOpenWildcard =
    "Markdown files (*.md;*.markdown;*.mdown;*.mkd)|"
    "*.md;*.markdown;*.mdown;*.mkd|All files (*.*)|*.*";

std::string read_text_file(const std::string& path, bool& ok)
{
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    ok = true;
    return buffer.str();
}

// A file:/// URL for the document's own folder, with the trailing slash
// render_document() requires so relative images resolve.
std::string base_href_for(const std::string& path)
{
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (dir.empty()) {
        dir = std::filesystem::current_path();
    }
    std::string text = dir.generic_string();
    if (text.empty() || text.back() != '/') {
        text += '/';
    }
    return "file:///" + text;
}

}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "MD Boss"),
      render_timer_(this)
{
    config_.load();
    SetSize(config_.window_width(), config_.window_height());
    SetMinSize(wxSize(640, 400));

    build_menu();
    build_panes();
    bind_events();
    update_title();
}

void MainFrame::build_menu()
{
    auto* file = new wxMenu();
    file->Append(wxID_OPEN, "&Open…\tCtrl+O");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");

    auto* view = new wxMenu();
    view->AppendCheckItem(kIdToggleFrontMatter,
                          "Hide &YAML front matter\tCtrl+Y");
    view->Check(kIdToggleFrontMatter, config_.hide_front_matter());

    auto* bar = new wxMenuBar();
    bar->Append(file, "&File");
    bar->Append(view, "&View");
    SetMenuBar(bar);
}

void MainFrame::build_panes()
{
    split_ = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize,
                                  wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
    split_->SetMinimumPaneSize(160);

    editor_ = new wxStyledTextCtrl(split_, wxID_ANY);
    // Scintilla gives the line-number gutter and current-line highlight the
    // Python app paints by hand in CodeEditor.
    editor_->SetLexer(wxSTC_LEX_MARKDOWN);
    editor_->SetMarginType(0, wxSTC_MARGIN_NUMBER);
    editor_->SetMarginWidth(0, 48);
    editor_->SetCaretLineVisible(true);
    editor_->SetCaretLineBackground(wxColour(245, 245, 245));
    editor_->SetWrapMode(wxSTC_WRAP_WORD);
    editor_->StyleSetFaceName(wxSTC_STYLE_DEFAULT, "Consolas");
    editor_->StyleSetSize(wxSTC_STYLE_DEFAULT, 10);
    editor_->StyleClearAll();

    preview_ = new PreviewPane(split_);
    preview_->set_on_scrolled([this](double ratio) {
        on_preview_scrolled(ratio);
    });

    split_->SplitVertically(editor_, preview_, config_.editor_sash());

    // The splitter must be in a sizer.  wxFrame will stretch a lone child to
    // fill, but that fallback does not drive a proper layout pass, so the
    // splitter kept sizing its panes against stale geometry -- which is what
    // pushed the preview's web view out past the right edge of the window.
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(split_, 1, wxEXPAND);
    SetSizer(sizer);
    Layout();
}

void MainFrame::bind_events()
{
    Bind(wxEVT_MENU, &MainFrame::on_open, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::on_save, this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::on_exit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_front_matter, this,
         kIdToggleFrontMatter);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::on_close, this);
    Bind(wxEVT_TIMER, &MainFrame::on_render_timer, this);

    editor_->Bind(wxEVT_STC_CHANGE, &MainFrame::on_text_changed, this);
    editor_->Bind(wxEVT_STC_UPDATEUI, &MainFrame::on_editor_scrolled, this);
}

bool MainFrame::open_path(const std::string& path)
{
    assert(!path.empty() && "open_path needs a path");
    if (!confirm_discard()) {
        return false;
    }
    bool ok = false;
    const std::string text = read_text_file(path, ok);
    if (!ok) {
        wxMessageBox("Could not read:\n" + wxString::FromUTF8(path),
                     "MD Boss", wxOK | wxICON_ERROR, this);
        return false;
    }

    editor_->SetText(wxString::FromUTF8(text));
    editor_->EmptyUndoBuffer();
    current_path_ = path;
    dirty_ = false;
    config_.push_recent(path);
    update_title();
    render_preview();
    return true;
}

void MainFrame::on_open(wxCommandEvent&)
{
    wxFileDialog dialog(this, "Open Markdown file", "", "", kOpenWildcard,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    open_path(std::string(dialog.GetPath().ToUTF8()));
}

void MainFrame::on_save(wxCommandEvent&)
{
    if (current_path_.empty()) {
        wxFileDialog dialog(this, "Save Markdown file", "", "", kOpenWildcard,
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        current_path_ = std::string(dialog.GetPath().ToUTF8());
    }
    if (save_to(current_path_)) {
        dirty_ = false;
        update_title();
    }
}

bool MainFrame::save_to(const std::string& path)
{
    assert(!path.empty() && "save_to needs a path");
    std::ofstream stream(std::filesystem::path(path),
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
        wxMessageBox("Could not write:\n" + wxString::FromUTF8(path),
                     "MD Boss", wxOK | wxICON_ERROR, this);
        return false;
    }
    const wxScopedCharBuffer text = editor_->GetText().ToUTF8();
    stream.write(text.data(), static_cast<std::streamsize>(text.length()));
    if (!stream.good()) {
        wxMessageBox("Write failed:\n" + wxString::FromUTF8(path), "MD Boss",
                     wxOK | wxICON_ERROR, this);
        return false;
    }
    return true;
}

void MainFrame::on_exit(wxCommandEvent&)
{
    Close(false);
}

void MainFrame::on_toggle_front_matter(wxCommandEvent& event)
{
    config_.set_hide_front_matter(event.IsChecked());
    render_preview();
}

void MainFrame::on_text_changed(wxStyledTextEvent& event)
{
    if (!dirty_) {
        dirty_ = true;
        update_title();
    }
    // Coalesce keystrokes: restart the timer rather than render each one.
    render_timer_.Start(kRenderDebounceMs, wxTIMER_ONE_SHOT);
    event.Skip();
}

void MainFrame::on_render_timer(wxTimerEvent&)
{
    render_preview();
}

void MainFrame::render_preview()
{
    assert(preview_ != nullptr && "preview must exist before rendering");
    const std::string markdown(editor_->GetText().ToUTF8());
    const std::string base = current_path_.empty()
                                 ? std::string("file:///")
                                 : base_href_for(current_path_);
    const std::string title =
        current_path_.empty()
            ? std::string("MD Boss")
            : std::filesystem::path(current_path_).filename().string();

    preview_->show_page(mdrender::render_document(
        markdown, base, title, config_.hide_front_matter()));
}

void MainFrame::on_editor_scrolled(wxStyledTextEvent& event)
{
    event.Skip();
    if (suppress_editor_scroll_) {
        return;
    }
    sync_preview_from_editor();
}

void MainFrame::sync_preview_from_editor()
{
    const int first = editor_->GetFirstVisibleLine();
    const int visible = editor_->LinesOnScreen();
    const int total = editor_->GetLineCount();
    const int span = total - visible;
    if (span <= 0) {
        return;
    }
    const double ratio = static_cast<double>(first) / span;
    suppress_preview_scroll_ = true;
    preview_->scroll_to(ratio);
    suppress_preview_scroll_ = false;
}

void MainFrame::on_preview_scrolled(double ratio)
{
    if (suppress_preview_scroll_) {
        return;
    }
    const int visible = editor_->LinesOnScreen();
    const int total = editor_->GetLineCount();
    const int span = total - visible;
    if (span <= 0) {
        return;
    }
    suppress_editor_scroll_ = true;
    editor_->SetFirstVisibleLine(static_cast<int>(ratio * span));
    suppress_editor_scroll_ = false;
}

void MainFrame::update_title()
{
    wxString name = "Untitled";
    if (!current_path_.empty()) {
        name = wxString::FromUTF8(
            std::filesystem::path(current_path_).filename().string());
    }
    SetTitle(wxString(dirty_ ? "*" : "") + name + " - MD Boss");
}

bool MainFrame::confirm_discard()
{
    if (!dirty_) {
        return true;
    }
    const int answer =
        wxMessageBox("Save changes to the current document?", "MD Boss",
                     wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL) {
        return false;
    }
    if (answer == wxYES) {
        wxCommandEvent unused;
        on_save(unused);
        return !dirty_;
    }
    return true;
}

void MainFrame::on_close(wxCloseEvent& event)
{
    if (event.CanVeto() && !confirm_discard()) {
        event.Veto();
        return;
    }
    const wxSize size = GetSize();
    config_.set_window_size(size.GetWidth(), size.GetHeight());
    if (split_ != nullptr) {
        config_.set_editor_sash(split_->GetSashPosition());
    }
    config_.save();
    event.Skip();
}

}  // namespace mdboss
