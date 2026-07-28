#include "MainFrame.h"

#include <wx/choicdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/sizer.h>
#include <wx/textfile.h>
#include <wx/stdpaths.h>
#include <wx/accel.h>
#include <wx/artprov.h>
#include <wx/toolbar.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "DropTarget.h"
#include "FileAssoc.h"
#include "FoldersDialog.h"
#include "Version.h"
#include "HelpDialog.h"
#include "PathUtf8.h"
#include "SingleInstance.h"
#include "Templates.h"
#include "mdrender/MdRender.h"

namespace mdboss {
namespace {

// Matches app.py's PREVIEW_DEBOUNCE_MS: long enough that typing does not
// re-render on every keystroke, short enough to feel live.
constexpr int kRenderDebounceMs = 300;

constexpr int kIdToggleFrontMatter = wxID_HIGHEST + 1;
constexpr int kIdManageFolders = wxID_HIGHEST + 2;
constexpr int kIdToggleFavorite = wxID_HIGHEST + 3;
constexpr int kIdNewFromTemplate = wxID_HIGHEST + 4;
constexpr int kIdOpenTemplates = wxID_HIGHEST + 5;
constexpr int kIdRefresh = wxID_HIGHEST + 6;
constexpr int kIdToggleFiles = wxID_HIGHEST + 7;
constexpr int kIdToggleOutline = wxID_HIGHEST + 8;
constexpr int kIdToggleEditor = wxID_HIGHEST + 9;
constexpr int kIdFileTypes = wxID_HIGHEST + 10;
constexpr int kIdHelp = wxID_HIGHEST + 11;

// Kept in step with app.py's MARKDOWN_EXTS.
const char* const kOpenWildcard =
    "Markdown files (*.md;*.markdown;*.mdown;*.mkd;*.mdwn)|"
    "*.md;*.markdown;*.mdown;*.mkd;*.mdwn|All files (*.*)|*.*";

std::string read_text_file(const std::string& path, bool& ok)
{
    std::ifstream stream(path_from_utf8(path), std::ios::binary);
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
    std::filesystem::path dir = path_from_utf8(path).parent_path();
    if (dir.empty()) {
        dir = std::filesystem::current_path();
    }
    // generic_u8string(): forward slashes for the URL, and UTF-8 rather than
    // the ANSI conversion string() would do (which throws on unmappable
    // characters).
    const std::u8string generic = dir.generic_u8string();
    std::string text(reinterpret_cast<const char*>(generic.data()),
                     generic.size());
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

    // Take the window icon from the executable's own resource, so there is
    // one icon rather than a separate copy to keep in step.  An icon bundle
    // picks the right size for the title bar, the task bar and Alt-Tab.
    const wxIconBundle icons(wxStandardPaths::Get().GetExecutablePath(),
                             wxBITMAP_TYPE_ICO);
    if (icons.GetIconCount() > 0) {
        SetIcons(icons);
    }

    build_menu();
    build_panes();
    build_toolbar();   // after the panes: the toggles reflect their state
    bind_events();
    update_title();
}

void MainFrame::build_menu()
{
    auto* file = new wxMenu();
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(kIdNewFromTemplate, L"New from &template…\tCtrl+Shift+N");
    file->Append(wxID_OPEN, L"&Open…\tCtrl+O");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->AppendSeparator();
    file->Append(kIdOpenTemplates, "Open &templates folder");
    file->Append(kIdManageFolders, L"&Manage folders…");
    file->Append(kIdToggleFavorite, "Add to &favorites\tCtrl+D");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");

    auto* view = new wxMenu();
    view->AppendCheckItem(kIdToggleFrontMatter,
                          "Hide &YAML front matter\tCtrl+Y");
    view->Check(kIdToggleFrontMatter, config_.hide_front_matter());

    auto* help = new wxMenu();
    // F1 rides the menu item, so it needs no accelerator table entry.
    help->Append(kIdHelp, "&Help\tF1");
    help->AppendSeparator();
    help->Append(wxID_ABOUT, wxString("&About ") + kAppName + L"…");

    auto* bar = new wxMenuBar();
    bar->Append(file, "&File");
    bar->Append(view, "&View");
    bar->Append(help, "&Help");
    SetMenuBar(bar);
}

void MainFrame::build_toolbar()
{
    // Icons from wxArtProvider rather than a shipped bitmap set: they follow
    // the platform's own theme and there is nothing extra to package.  The
    // button's label is carried in the tooltip, since the buttons no longer
    // show text.
    struct Tool {
        int id;
        const wchar_t* label;
        const wxArtID art;
        const wchar_t* detail;   // appended after the label in the tooltip
        bool check;
        bool separator_after;
    };

    const Tool tools[] = {
        {kIdManageFolders, L"Manage folders…", wxART_FOLDER_OPEN,
         L"Add, remove, or reorder root folders", false, false},
        {kIdRefresh, L"Refresh", wxART_REFRESH, L"Rescan all roots (F5)",
         false, true},

        {wxID_OPEN, L"Open…", wxART_FILE_OPEN,
         L"Open a Markdown file from anywhere on disk (Ctrl+O)", false, false},
        {wxID_NEW, L"New", wxART_NEW, L"Create a new Markdown file (Ctrl+N)",
         false, false},
        {kIdNewFromTemplate, L"New from template…", wxART_NORMAL_FILE,
         L"Create a new file from a template", false, false},
        {wxID_SAVE, L"Save", wxART_FILE_SAVE,
         L"Save the current document (Ctrl+S)", false, true},

        // Toggles ordered to match the columns: Files | Outline | Edit.
        {kIdToggleFiles, L"Files", wxART_LIST_VIEW,
         L"Show or hide the file tree", true, false},
        {kIdToggleOutline, L"Outline", wxART_REPORT_VIEW,
         L"Show or hide the outline pane", true, false},
        {kIdToggleEditor, L"Edit", wxART_EDIT,
         L"Show or hide the source editor", true, false},
        {kIdToggleFrontMatter, L"Hide YAML", wxART_MINUS,
         L"Hide a YAML front-matter block at the top of the file", true, true},

        // Help lives on the Help menu, not here: it is not something reached
        // often enough to earn a permanent button.
        {kIdFileTypes, L"File types…", wxART_EXECUTABLE_FILE,
         L"Register MD Boss as a handler for Markdown files", false, false},
    };

    wxToolBar* bar = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT);
    for (const Tool& tool : tools) {
        // The tooltip leads with the label, so an icon whose meaning is not
        // obvious still names itself.
        // The separator must be a WIDE literal.  A narrow one holding an
        // em-dash is handed to wxString as bytes and decoded in the ANSI code
        // page, which showed up in every tooltip as mojibake.  A test below
        // now scans the sources for this, because it is easy to repeat.
        const wxString tip =
            wxString(tool.label) + L"  —  " + wxString(tool.detail);
        const wxBitmapBundle icon =
            wxArtProvider::GetBitmapBundle(tool.art, wxART_TOOLBAR);
        if (tool.check) {
            bar->AddCheckTool(tool.id, tool.label, icon, wxBitmapBundle(), tip);
        } else {
            bar->AddTool(tool.id, tool.label, icon, tip);
        }
        if (tool.separator_after) {
            bar->AddSeparator();
        }
    }

    bar->ToggleTool(kIdToggleFiles, config_.show_files());
    bar->ToggleTool(kIdToggleOutline, config_.show_outline());
    bar->ToggleTool(kIdToggleEditor, config_.show_editor());
    bar->ToggleTool(kIdToggleFrontMatter, config_.hide_front_matter());
    bar->Realize();

    // Refresh has no menu item, so F5 needs an accelerator of its own.  F1
    // does not: it rides the Help menu entry.
    const wxAcceleratorEntry accelerators[] = {
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F5, kIdRefresh),
    };
    SetAcceleratorTable(wxAcceleratorTable(1, accelerators));
}

void MainFrame::build_panes()
{
    // files | (outline | (editor | preview)).  wxSplitterWindow is binary, so
    // three of them nest to make four panes.
    files_split_ = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition,
                                        wxDefaultSize,
                                        wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
    files_split_->SetMinimumPaneSize(140);

    recent_split_ = new wxSplitterWindow(files_split_, wxID_ANY,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
    recent_split_->SetMinimumPaneSize(80);
    recent_ = new PathListPanel(recent_split_, "Recent", "");
    recent_->set_on_activate([this](const std::string& p) { open_path(p); });
    recent_->set_on_clear([this] {
        config_.clear_recents();
        config_.save();
        refresh_lists();
    });

    favorites_split_ = new wxSplitterWindow(recent_split_, wxID_ANY,
                                            wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
    favorites_split_->SetMinimumPaneSize(80);
    favorites_ =
        new PathListPanel(favorites_split_, "Favorites", "&Remove favorite");
    favorites_->set_on_activate([this](const std::string& p) { open_path(p); });
    favorites_->set_on_extra([this](const std::string& p) {
        config_.remove_favorite(p);
        config_.save();
        refresh_lists();
    });
    favorites_->set_on_clear([this] {
        config_.clear_favorites();
        config_.save();
        refresh_lists();
    });

    files_ = new FileTreePanel(favorites_split_);
    files_->set_on_open([this](const std::string& path) { open_path(path); });
    files_->set_favorite_hooks(
        [this](const std::string& path) {
            if (config_.is_favorite(path)) {
                config_.remove_favorite(path);
            } else {
                config_.add_favorite(path);
            }
            config_.save();
            refresh_lists();
        },
        [this](const std::string& path) { return config_.is_favorite(path); });

    outline_split_ = new wxSplitterWindow(files_split_, wxID_ANY,
                                          wxDefaultPosition, wxDefaultSize,
                                          wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
    outline_split_->SetMinimumPaneSize(140);

    outline_ = new OutlinePanel(outline_split_);
    outline_->set_on_activate([this](const std::string& slug) {
        preview_->scroll_to_anchor(slug);
    });

    split_ = new wxSplitterWindow(outline_split_, wxID_ANY, wxDefaultPosition,
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
    outline_split_->SplitVertically(outline_, split_, config_.outline_sash());
    favorites_split_->SplitHorizontally(favorites_, files_,
                                        config_.favorites_sash());
    recent_split_->SplitHorizontally(recent_, favorites_split_,
                                     config_.recent_sash());
    files_split_->SplitVertically(recent_split_, outline_split_,
                                  config_.files_sash());

    // The top-level child goes in a sizer: wxFrame will stretch a lone child
    // to fill, but that fallback does not drive a proper layout pass.
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(files_split_, 1, wxEXPAND);
    SetSizer(sizer);
    Layout();

    // A drop is consumed by whichever child window is under the cursor, so
    // every pane the user might aim at needs its own target.  Each
    // SetDropTarget takes ownership, hence a fresh instance per window.
    const auto open_dropped = [this](const std::string& path) {
        open_path(path);
    };
    for (wxWindow* target : {static_cast<wxWindow*>(this),
                             static_cast<wxWindow*>(editor_),
                             static_cast<wxWindow*>(preview_),
                             static_cast<wxWindow*>(files_),
                             static_cast<wxWindow*>(outline_),
                             static_cast<wxWindow*>(recent_),
                             static_cast<wxWindow*>(favorites_)}) {
        target->SetDropTarget(new DocumentDropTarget(open_dropped));
    }

    files_->set_roots(config_.roots());
    refresh_lists();

    // Re-apply hidden columns from last time.  The sash positions are already
    // loaded, so a pane shown again returns to the width it had.
    hidden_files_sash_ = config_.files_sash();
    hidden_outline_sash_ = config_.outline_sash();
    hidden_editor_sash_ = config_.editor_sash();
    if (!config_.show_files()) {
        files_split_->Unsplit(recent_split_);
    }
    if (!config_.show_outline()) {
        outline_split_->Unsplit(outline_);
    }
    if (!config_.show_editor()) {
        split_->Unsplit(editor_);
    }
}

WXLRESULT MainFrame::MSWWindowProc(WXUINT message, WXWPARAM wparam,
                                   WXLPARAM lparam)
{
    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        if (data != nullptr && data->dwData == instance_message_id() &&
            data->lpData != nullptr && data->cbData > 1) {
            // The sender is blocked in SendMessage, so copy the payload out
            // before doing anything that could pump messages.
            const std::string path(static_cast<const char*>(data->lpData),
                                   data->cbData - 1);
            Raise();
            CallAfter([this, path] { open_path(path); });
            return TRUE;
        }
    }
    return wxFrame::MSWWindowProc(message, wparam, lparam);
}

void MainFrame::refresh_lists()
{
    recent_->set_paths(config_.recents());
    favorites_->set_paths(config_.favorites());
}

void MainFrame::bind_events()
{
    Bind(wxEVT_MENU, &MainFrame::on_open, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::on_save, this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::on_exit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_front_matter, this,
         kIdToggleFrontMatter);
    Bind(wxEVT_MENU, &MainFrame::on_manage_folders, this, kIdManageFolders);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_favorite, this, kIdToggleFavorite);
    Bind(wxEVT_MENU, &MainFrame::on_new, this, wxID_NEW);
    Bind(wxEVT_MENU, &MainFrame::on_new_from_template, this,
         kIdNewFromTemplate);
    Bind(wxEVT_MENU, &MainFrame::on_open_templates_folder, this,
         kIdOpenTemplates);
    Bind(wxEVT_MENU, &MainFrame::on_refresh, this, kIdRefresh);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_files, this, kIdToggleFiles);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_outline, this, kIdToggleOutline);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_editor, this, kIdToggleEditor);
    Bind(wxEVT_MENU, &MainFrame::on_file_types, this, kIdFileTypes);
    Bind(wxEVT_MENU, &MainFrame::on_help, this, kIdHelp);
    Bind(wxEVT_MENU, &MainFrame::on_about, this, wxID_ABOUT);
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
    config_.save();
    refresh_lists();
    update_title();
    render_preview();
    return true;
}

void MainFrame::on_open(wxCommandEvent&)
{
    wxFileDialog dialog(this, L"Open Markdown file", "", "", kOpenWildcard,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    open_path(std::string(dialog.GetPath().ToUTF8()));
}

void MainFrame::on_save(wxCommandEvent&)
{
    if (current_path_.empty()) {
        wxFileDialog dialog(this, L"Save Markdown file", "", "", kOpenWildcard,
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
    std::ofstream stream(path_from_utf8(path),
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
            : path_to_utf8(path_from_utf8(current_path_).filename());

    preview_->show_page(mdrender::render_document(
        markdown, base, title, config_.hide_front_matter()));

    // Same source, same strip_yaml, so the slugs here are the ids in the page
    // that was just rendered.
    outline_->set_headings(
        mdrender::extract_outline(markdown, config_.hide_front_matter()));
}

void MainFrame::on_toggle_favorite(wxCommandEvent&)
{
    if (current_path_.empty()) {
        return;   // nothing open; silently no-op rather than favourite ""
    }
    if (config_.is_favorite(current_path_)) {
        config_.remove_favorite(current_path_);
    } else {
        config_.add_favorite(current_path_);
    }
    config_.save();
    refresh_lists();
}

void MainFrame::on_new(wxCommandEvent&)
{
    if (!confirm_discard()) {
        return;
    }
    editor_->SetText("");
    editor_->EmptyUndoBuffer();
    current_path_.clear();
    dirty_ = false;
    update_title();
    render_preview();
}

void MainFrame::on_new_from_template(wxCommandEvent&)
{
    const std::vector<std::pair<std::string, std::string>> templates =
        list_templates();
    if (templates.empty()) {
        const int answer = wxMessageBox(
            "No templates yet.\n\nOpen the templates folder to add some?",
            "MD Boss", wxYES_NO | wxICON_QUESTION, this);
        if (answer == wxYES) {
            wxCommandEvent unused;
            on_open_templates_folder(unused);
        }
        return;
    }

    wxArrayString names;
    for (const auto& [name, path] : templates) {
        names.Add(wxString::FromUTF8(name));
    }
    const int choice = wxGetSingleChoiceIndex("Start from which template?",
                                              "MD Boss", names, this);
    if (choice < 0) {
        return;
    }
    if (!confirm_discard()) {
        return;
    }

    const wxString title = wxGetTextFromUser(
        "Title for the new document:", "MD Boss", "Untitled", this);
    if (title.IsEmpty()) {
        return;
    }

    bool ok = false;
    const std::string body =
        read_text_file(templates[static_cast<std::size_t>(choice)].second, ok);
    if (!ok) {
        wxMessageBox("Could not read that template.", "MD Boss",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    editor_->SetText(wxString::FromUTF8(
        apply_template(body, std::string(title.ToUTF8()))));
    editor_->EmptyUndoBuffer();
    // Deliberately unsaved and unnamed: the document exists only in the
    // editor until the user chooses where it belongs.
    current_path_.clear();
    dirty_ = true;
    update_title();
    render_preview();
}

void MainFrame::on_open_templates_folder(wxCommandEvent&)
{
    seed_templates();   // first use creates the folder and its starters
    const wxString dir = wxString::FromUTF8(templates_dir());
    wxExecute("explorer.exe \"" + dir + "\"", wxEXEC_ASYNC);
}

void MainFrame::on_refresh(wxCommandEvent&)
{
    files_->refresh();
}

void MainFrame::on_toggle_files(wxCommandEvent&)
{
    if (files_split_->IsSplit()) {
        hidden_files_sash_ = files_split_->GetSashPosition();
        files_split_->Unsplit(recent_split_);
    } else {
        files_split_->SplitVertically(recent_split_, outline_split_,
                                      hidden_files_sash_);
    }
}

void MainFrame::on_toggle_outline(wxCommandEvent&)
{
    if (outline_split_->IsSplit()) {
        hidden_outline_sash_ = outline_split_->GetSashPosition();
        outline_split_->Unsplit(outline_);
    } else {
        outline_split_->SplitVertically(outline_, split_,
                                        hidden_outline_sash_);
    }
}

void MainFrame::on_toggle_editor(wxCommandEvent&)
{
    if (split_->IsSplit()) {
        hidden_editor_sash_ = split_->GetSashPosition();
        split_->Unsplit(editor_);
    } else {
        split_->SplitVertically(editor_, preview_, hidden_editor_sash_);
    }
}

void MainFrame::on_file_types(wxCommandEvent&)
{
    const std::string command = handler_command();
    const bool registered = is_registered(command);

    wxString message;
    message << (registered
                    ? "MD Boss is registered as a Markdown handler.\n\n"
                    : "MD Boss is not currently the registered handler.\n\n");
    message << "Windows does not let an application make itself the "
               "default.\nRegistering only adds MD Boss to \"Open with\" and "
               "to\nSettings > Default apps, where you can choose it.\n\n";
    message << "Command:\n" << wxString::FromUTF8(command);

    const int answer = wxMessageBox(
        message + (registered ? "\n\nUnregister?" : "\n\nRegister now?"),
        "MD Boss file types", wxYES_NO | wxICON_INFORMATION, this);
    if (answer != wxYES) {
        return;
    }

    const RegPlan plan = current_registration_plan();
    if (registered) {
        remove_registration(plan);
    } else if (!apply_registration(plan)) {
        wxMessageBox("Registration did not complete.", "MD Boss",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    notify_assoc_changed();
}

void MainFrame::on_help(wxCommandEvent&)
{
    // A window of its own rather than opening HELP.md as a document: reading
    // the help should not replace whatever the user was editing.
    HelpDialog dialog(this);
    dialog.ShowModal();
}

void MainFrame::on_about(wxCommandEvent&)
{
    show_about_box(this);
}

void MainFrame::on_manage_folders(wxCommandEvent&)
{
    FoldersDialog dialog(this, config_.roots());
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    config_.set_roots(dialog.roots());
    config_.save();
    files_->set_roots(config_.roots());
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
            path_to_utf8(path_from_utf8(current_path_).filename()));
    }
    SetTitle(wxString(dirty_ ? "*" : "") + name + " - MD Boss");
}

bool MainFrame::confirm_discard()
{
    if (!dirty_) {
        return true;
    }
    const int answer =
        wxMessageBox(L"Save changes to the current document?", "MD Boss",
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
    // Save the sash a hidden pane *would* return to, not the meaningless
    // value an unsplit splitter reports.
    if (split_ != nullptr) {
        config_.set_show_editor(split_->IsSplit());
        config_.set_editor_sash(split_->IsSplit() ? split_->GetSashPosition()
                                                  : hidden_editor_sash_);
    }
    if (outline_split_ != nullptr) {
        config_.set_show_outline(outline_split_->IsSplit());
        config_.set_outline_sash(outline_split_->IsSplit()
                                     ? outline_split_->GetSashPosition()
                                     : hidden_outline_sash_);
    }
    if (files_split_ != nullptr) {
        config_.set_show_files(files_split_->IsSplit());
        config_.set_files_sash(files_split_->IsSplit()
                                   ? files_split_->GetSashPosition()
                                   : hidden_files_sash_);
    }
    if (recent_split_ != nullptr) {
        config_.set_recent_sash(recent_split_->GetSashPosition());
    }
    if (favorites_split_ != nullptr) {
        config_.set_favorites_sash(favorites_split_->GetSashPosition());
    }
    config_.save();
    event.Skip();
}

}  // namespace mdboss
