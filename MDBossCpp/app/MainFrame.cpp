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

#include <algorithm>
#include <cmath>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "DropTarget.h"
#include "Favorites.h"
#include "FileAssoc.h"
#include "FileScan.h"
#include "FoldersDialog.h"
#include "Version.h"
#include "HelpDialog.h"
#include "PathUtf8.h"
#include "SingleInstance.h"
#include "Templates.h"
#include "Updater.h"
#include "mdrender/MdRender.h"

namespace mdboss {
namespace {

// Matches app.py's PREVIEW_DEBOUNCE_MS: long enough that typing does not
// re-render on every keystroke, short enough to feel live.
constexpr int kRenderDebounceMs = 300;
// How long the preview's scroll echo is ignored after we scroll it
// ourselves.  ExecuteScript is asynchronous: it returns before the preview
// has moved, and the echo arrives later still, so the guard has to outlive
// the call rather than wrap it.  Matches the Python app's 120 ms.
constexpr int kScrollEchoMs = 120;

// Distinct ids because a wxTimer constructed with an owner posts its events
// to that owner: two timers on this frame with the default id would both
// arrive at whichever handler was bound without one.
constexpr int kIdRenderTimer = wxID_HIGHEST + 20;
constexpr int kIdScrollEchoTimer = wxID_HIGHEST + 21;

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
constexpr int kIdCheckUpdates = wxID_HIGHEST + 12;
// Not wxID_CLOSE: that is the frame's own close command, and wx routes it to
// the window rather than to a handler of ours -- the app would exit.
constexpr int kIdCloseDocument = wxID_HIGHEST + 13;
constexpr int kIdInsertImage = wxID_HIGHEST + 14;
// Snippets take ids from here up, one per kSnippets entry.  Bounded (Rule of
// 10): the array is fixed at compile time, so the range cannot run past the
// next constant.
constexpr int kIdSnippetBase = wxID_HIGHEST + 40;

// Matching the formats a browser will actually display, since the preview is
// one.  TIFF and PSD are deliberately absent: they would insert a reference
// that renders as a broken image.
const char* const kImageWildcard =
    "Image files (*.png;*.jpg;*.jpeg;*.gif;*.svg;*.webp;*.bmp)|"
    "*.png;*.jpg;*.jpeg;*.gif;*.svg;*.webp;*.bmp|All files (*.*)|*.*";

// The five GitHub alert kinds, in GitHub's own order of severity.  The
// renderer already turns these into styled callouts -- see
// mdrender/src/Preprocess.cpp -- so what a snippet inserts previews the same
// way it will on GitHub.
//
// The marker line carries no trailing spaces.  GitHub's documentation shows
// two on some of these, which Markdown reads as a hard line break; the alert
// syntax does not need one and the house style bans them.
struct Snippet {
    const wchar_t* label;
    const char* body;
};

const Snippet kSnippets[] = {
    {L"&Note",
     "> [!NOTE]\n"
     "> Highlights information that users should take into account, even when "
     "skimming.\n"},
    {L"&Tip",
     "> [!TIP]\n"
     "> Optional information to help a user be more successful.\n"},
    {L"&Important",
     "> [!IMPORTANT]\n"
     "> Crucial information necessary for users to succeed.\n"},
    {L"&Warning",
     "> [!WARNING]\n"
     "> Critical content demanding immediate user attention due to potential "
     "risks.\n"},
    {L"&Caution",
     "> [!CAUTION]\n"
     "> Negative potential consequences of an action.\n"},
    // Rendered by the bundled mermaid.js -- see HtmlRenderer.cpp, which turns a
    // ```mermaid fence into <pre class="mermaid"> rather than a code block.
    //
    // Flowchart node labels are quoted because the parser needs them to be:
    // an unquoted label containing &, / or parentheses is a silent parse
    // failure, and a diagram that fails to parse renders as nothing at all.
    {L"&Mermaid diagram",
     "```mermaid\n"
     "flowchart LR\n"
     "    A[\"Start\"] --> B[\"Next step\"]\n"
     "    B --> C[\"Done\"]\n"
     "```\n"},
    // Three columns of two rows: enough to show the shape, small enough to
    // delete what is not wanted.  The separator row is what makes it a table
    // rather than three lines of text, so it is spelled out rather than left
    // to the user to remember.
    {L"Ta&ble",
     "| Column | Column | Column |\n"
     "|--------|--------|--------|\n"
     "|        |        |        |\n"
     "|        |        |        |\n"},
};

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
    return strip_utf8_bom(buffer.str());
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
      render_timer_(this, kIdRenderTimer),
      scroll_echo_timer_(this, kIdScrollEchoTimer),
      // Safe to hand `this` over during construction: the watcher only ever
      // calls back from the event loop, which is not running yet.
      watcher_([this](const std::string& path, bool still_exists) {
          on_document_changed(path, still_exists);
      })
{
    config_.load();
    // At startup, as the Python app does: a starter added in this version has
    // to reach a profile whose templates folder already exists, and waiting
    // for someone to open that folder means it never appears in the menus
    // where templates are actually picked.
    if (seed_templates(config_)) {
        config_.save();
    }
    SetSize(config_.window_width(), config_.window_height());
    SetMinSize(wxSize(640, 400));

    // Load the icon as a RESOURCE, not by reading the exe as an image file.
    // The file form needs an ICO image handler registered and, without one,
    // pops "No image handler for type 3 defined" at every launch -- while the
    // title bar looked right anyway, because Windows falls back to the exe's
    // own first icon. "#1" is the ordinal the .rc assigns it.
    wxIcon icon;
    if (icon.LoadFile("#1", wxBITMAP_TYPE_ICO_RESOURCE)) {
        SetIcon(icon);
    }

    CreateStatusBar();

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
    file->Append(kIdCloseDocument, "&Close document\tCtrl+W");
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

    auto* snippets = new wxMenu();
    for (std::size_t i = 0; i < std::size(kSnippets); ++i) {   // bounded
        snippets->Append(kIdSnippetBase + static_cast<int>(i),
                         kSnippets[i].label);
    }
    // Below a separator: the others insert fixed text, this one asks first.
    snippets->AppendSeparator();
    // "&f", not "&i": Important already claims I, and two items sharing a
    // mnemonic makes the key cycle between them instead of choosing one.
    snippets->Append(kIdInsertImage, L"Insert image &file…");

    auto* help = new wxMenu();
    // F1 rides the menu item, so it needs no accelerator table entry.
    help->Append(kIdHelp, "&Help\tF1");
    help->Append(kIdCheckUpdates, L"Check for &updates…");
    help->AppendSeparator();
    help->Append(wxID_ABOUT, wxString("&About ") + kAppName + L"…");

    auto* bar = new wxMenuBar();
    bar->Append(file, "&File");
    bar->Append(view, "&View");
    bar->Append(snippets, "&Snippets");
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
         L"Save the current document (Ctrl+S)", false, false},
        {kIdCloseDocument, L"Close", wxART_CLOSE,
         L"Close the open document and empty the editor (Ctrl+W)", false,
         true},

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
            // The greyed bitmap is supplied rather than left to wxMSW to
            // generate: the one it made for wxART_CLOSE was indistinguishable
            // from the enabled icon, so a disabled Close looked perfectly
            // clickable.  The tool really was disabled -- it just did not say
            // so, which is worse than either state on its own.
            const wxBitmap normal = icon.GetBitmap(wxDefaultSize);
            const wxBitmapBundle greyed =
                normal.IsOk()
                    ? wxBitmapBundle::FromBitmap(normal.ConvertToDisabled())
                    : wxBitmapBundle();
            bar->AddTool(tool.id, tool.label, icon, greyed, wxITEM_NORMAL, tip);
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
    favorites_->add_menu_command(L"&Export favorites…",
                                 [this] { on_export_favorites(); });
    favorites_->add_menu_command(L"&Import favorites…",
                                 [this] { on_import_favorites(); });

    files_ = new FileTreePanel(favorites_split_);
    files_->set_on_open([this](const std::string& path) { open_path(path); });
    files_->set_on_import_to_inbox([this] { on_import_to_inbox(); });
    files_->set_manage_hooks(
        [this] {
            wxCommandEvent unused;
            on_open_templates_folder(unused);
        },
        [this] {
            wxCommandEvent unused;
            on_manage_folders(unused);
        });
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
    files_->set_flat_hooks(
        [this](const std::string& path) {
            return config_.is_flat_folder(path);
        },
        [this](const std::string& path) {
            config_.set_flat_folder(path, !config_.is_flat_folder(path));
            config_.save();
        });

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
    // Reopen the folders that were open last time.  Safe before the counting
    // scan finishes: the rebuild it triggers carries over whatever is on
    // screen, so this is restored rather than raced.
    files_->set_expanded_folders(config_.expanded_folders());
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
    Bind(wxEVT_MENU, &MainFrame::on_close_document, this, kIdCloseDocument);
    Bind(wxEVT_MENU, &MainFrame::on_snippet, this, kIdSnippetBase,
         kIdSnippetBase + static_cast<int>(std::size(kSnippets)) - 1);
    Bind(wxEVT_MENU, &MainFrame::on_insert_image, this, kIdInsertImage);
    Bind(wxEVT_MENU, &MainFrame::on_open_templates_folder, this,
         kIdOpenTemplates);
    Bind(wxEVT_MENU, &MainFrame::on_refresh, this, kIdRefresh);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_files, this, kIdToggleFiles);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_outline, this, kIdToggleOutline);
    Bind(wxEVT_MENU, &MainFrame::on_toggle_editor, this, kIdToggleEditor);
    Bind(wxEVT_MENU, &MainFrame::on_file_types, this, kIdFileTypes);
    Bind(wxEVT_MENU, &MainFrame::on_help, this, kIdHelp);
    Bind(wxEVT_MENU, &MainFrame::on_about, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::on_check_updates, this, kIdCheckUpdates);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::on_close, this);
    Bind(wxEVT_TIMER, &MainFrame::on_render_timer, this, kIdRenderTimer);
    Bind(wxEVT_TIMER, &MainFrame::on_scroll_echo_timer, this,
         kIdScrollEchoTimer);

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
    std::string text = read_text_file(path, ok);
    if (!ok) {
        wxMessageBox("Could not read:\n" + wxString::FromUTF8(path),
                     "MD Boss", wxOK | wxICON_ERROR, this);
        return false;
    }

    // Divergence from the Python app, which fails a non-UTF-8 file with a
    // decode error: the port offers to convert a recognisable legacy
    // encoding.  What it must never do is what shipped in v1.2.0 -- strict
    // FromUTF8 turning an undecodable file into an EMPTY editor, from which
    // one Ctrl+S wiped the document.
    bool needs_conversion = false;
    if (first_invalid_utf8(text) != std::string::npos) {
        const TextEncoding kind = detect_text_encoding(text);
        bool converted = false;
        std::string utf8;
        if (kind != TextEncoding::kBinary) {
            utf8 = convert_to_utf8(text, kind, converted);
        }
        if (!converted) {
            wxMessageBox(
                wxString::FromUTF8(
                    "Not opened: the file is not valid UTF-8 (first bad "
                    "byte at offset " +
                    std::to_string(first_invalid_utf8(text)) +
                    ") and no safe conversion exists.\n\n" + path),
                "MD Boss", wxOK | wxICON_ERROR, this);
            return false;
        }
        const int answer = wxMessageBox(
            wxString::FromUTF8("This file is encoded as " +
                               text_encoding_name(kind) +
                               ", not UTF-8:\n\n" + path +
                               "\n\nConvert it and open?  The file on disk "
                               "stays untouched until you save; saving "
                               "writes UTF-8."),
            "MD Boss", wxYES_NO | wxICON_QUESTION, this);
        if (answer != wxYES) {
            return false;
        }
        text = utf8;
        needs_conversion = true;
    }

    editor_->SetText(wxString::FromUTF8(text));
    editor_->EmptyUndoBuffer();
    current_path_ = path;
    // A converted document differs from the bytes on disk until it is
    // saved, and the dirty flag is what makes that visible.
    dirty_ = needs_conversion;
    watcher_.watch(current_path_);
    // Any warning on show belonged to the document being replaced.
    SetStatusText(wxString());
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
        // A document that has never been saved has no name to offer, but it
        // usually has a title -- typed into the New-from-template prompt, or
        // written as the first heading.  Offering it beats an empty box and
        // costs nothing when it is wrong: the dialog is still a dialog.
        const wxString suggested = wxString::FromUTF8(filename_from_title(
            mdrender::document_title(editor_->GetText().utf8_string())));
        wxFileDialog dialog(this, L"Save Markdown file", "", suggested,
                            kOpenWildcard,
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        current_path_ = std::string(dialog.GetPath().ToUTF8());
    }
    if (save_to(current_path_)) {
        dirty_ = false;
        // After the write, so the state adopted as "expected" is the one we
        // just produced.  Our own save raises the same event an outside edit
        // does; this is what stops it being reported back to us.
        watcher_.watch(current_path_);
        // A "your unsaved edits are kept" warning is about a conflict this
        // save has just resolved; leaving it up would keep warning about
        // edits that are now on disk.
        SetStatusText(wxString());
        update_title();
    }
}

bool MainFrame::save_to(const std::string& path)
{
    assert(!path.empty() && "save_to needs a path");
    // utf8_string(): one owned deep copy, no scoped-buffer aliasing.  The
    // checked write exists because v1.2.0 saved documents whose first 16
    // bytes the heap had already reclaimed -- six files lost their heads to
    // freed-block pointers.  Validating the buffer and reading the file back
    // turns that silent corruption into a refused save.
    std::string text = editor_->GetText().utf8_string();
    // A document started from the TechNote template carries the logo inline so
    // it renders while it is still unsaved and has no folder.  It has one now,
    // so put the .png beside it and swap in the relative reference every
    // hand-written tech note uses.  Silently a no-op for any other document.
    if (has_embedded_logo(text)) {
        const std::string localized = localize_embedded_logo(text, path);
        if (localized != text) {
            text = localized;
            // Keep the editor and the file in step: leaving the buffer holding
            // a data: URI the file no longer has would make the next save
            // re-do this, and the document watcher report our own write as an
            // outside edit.  The caret is clamped rather than tracked -- this
            // happens once, on the first save of a brand-new note.
            const int caret = editor_->GetCurrentPos();
            const int top = editor_->GetFirstVisibleLine();
            editor_->SetText(wxString::FromUTF8(text));
            editor_->GotoPos(std::min(caret, editor_->GetLength()));
            editor_->SetFirstVisibleLine(top);
        }
    }
    const std::string error = write_text_file_checked(path, text);
    if (!error.empty()) {
        wxMessageBox(wxString::FromUTF8(error) + "\n\n" +
                         wxString::FromUTF8(path),
                     "MD Boss", wxOK | wxICON_ERROR, this);
        return false;
    }
    return true;
}

void MainFrame::on_export_favorites()
{
    const std::vector<std::string>& favorites = config_.favorites();
    if (favorites.empty()) {
        wxMessageBox("You have no favorites to export.", "MD Boss",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxFileDialog dialog(this, "Export favorites", "", "mdboss-favorites.json",
                        "JSON files (*.json)|*.json",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    const std::string target = std::string(dialog.GetPath().ToUTF8());
    std::ofstream stream(path_from_utf8(target),
                         std::ios::binary | std::ios::trunc);
    const std::string text = favorites_to_json(favorites);
    if (stream) {
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    if (!stream || !stream.good()) {
        wxMessageBox("Could not write file:\n" + dialog.GetPath(), "MD Boss",
                     wxOK | wxICON_WARNING, this);
        return;
    }

    wxMessageBox(wxString::Format("Exported %zu favorite(s) to:\n",
                                  favorites.size()) +
                     dialog.GetPath(),
                 "MD Boss", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::on_import_favorites()
{
    wxFileDialog dialog(this, "Import favorites", "", "",
                        "JSON files (*.json)|*.json|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    bool ok = false;
    const std::string text =
        read_text_file(std::string(dialog.GetPath().ToUTF8()), ok);
    if (!ok) {
        wxMessageBox("Could not read file:\n" + dialog.GetPath(), "MD Boss",
                     wxOK | wxICON_WARNING, this);
        return;
    }

    const FavoritesFile file = parse_favorites_json(text);
    if (!file.parsed) {
        wxMessageBox("That file doesn't contain a favorites list.", "MD Boss",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    if (file.paths.empty()) {
        wxMessageBox("No favorites were found in that file.", "MD Boss",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    // Only worth asking when there is something to lose.
    bool merge = true;
    if (!config_.favorites().empty()) {
        const int answer = wxMessageBox(
            L"Merge with your current favorites?\n\n"
            L"Yes — add the imported files to your list\n"
            L"No — replace your current favorites",
            "MD Boss", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        if (answer == wxCANCEL) {
            return;
        }
        merge = answer == wxYES;
    }

    config_.set_favorites(merge_favorites(config_.favorites(), file.paths,
                                          merge, kMaxFavorites));
    config_.save();
    refresh_lists();
    wxMessageBox(wxString::Format("Your favorites list now has %zu item(s).",
                                  config_.favorites().size()),
                 "MD Boss", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::on_import_to_inbox()
{
    const std::string inbox = find_inbox(root_paths());
    if (inbox.empty()) {
        wxMessageBox(wxString(L"No ") + kInboxName + L" folder was found.\n\n"
                         L"Create a folder named " + kInboxName +
                         L" inside one of your root folders (or add one as a "
                         L"root), then try again.",
                     "MD Boss", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxFileDialog dialog(this, wxString(L"Import files into ") + kInboxName, "",
                        "", kOpenWildcard,
                        wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    wxArrayString chosen;
    dialog.GetPaths(chosen);

    std::vector<std::string> copied;
    std::vector<std::string> failed;
    const std::size_t limit = std::min<std::size_t>(chosen.GetCount(), 1000);
    for (std::size_t i = 0; i < limit; ++i) {   // bounded (Rule of 10)
        const std::string src = std::string(chosen[i].ToUTF8());
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path_from_utf8(src), ec) || ec) {
            continue;
        }
        // Already in the inbox: open it where it is rather than making a
        // second copy of a file the user can already see there.
        if (norm_path(path_to_utf8(path_from_utf8(src).parent_path())) ==
            norm_path(inbox)) {
            copied.push_back(src);
            continue;
        }
        const std::string dest =
            unique_dest(inbox, path_to_utf8(path_from_utf8(src).filename()));
        std::filesystem::copy_file(path_from_utf8(src), path_from_utf8(dest),
                                   ec);
        if (ec) {
            failed.push_back(src);
            continue;
        }
        copied.push_back(dest);
    }

    files_->refresh();
    if (!failed.empty()) {
        wxString names;
        for (const std::string& path : failed) {
            names += "\n" + wxString::FromUTF8(
                                path_to_utf8(path_from_utf8(path).filename()));
        }
        wxMessageBox(wxString(L"Could not copy these files into ") +
                         kInboxName + ":" + names,
                     "MD Boss", wxOK | wxICON_WARNING, this);
    }
    if (!copied.empty()) {
        open_path(copied.front());
    }
}

void MainFrame::on_document_changed(const std::string& path, bool still_exists)
{
    // The watch may outlive the document by a moment: a change reported for a
    // file we have since navigated away from is not ours to act on.
    if (path != current_path_) {
        return;
    }

    if (!still_exists) {
        // Emphatically not a reload -- there is nothing left to read.  The
        // buffer is now the only copy, so it is left exactly as it is.
        SetStatusText(L"Deleted or renamed on disk. The copy open here is "
                      L"now the only one — save it to write it back.");
        return;
    }

    if (dirty_) {
        SetStatusText(L"Changed on disk. Your unsaved edits are kept — "
                      L"save to overwrite the file, or reopen to discard.");
        return;
    }

    reload_from_disk();
}

void MainFrame::reload_from_disk()
{
    assert(!current_path_.empty() && "reload needs an open document");
    assert(!dirty_ && "reload must never discard unsaved edits");

    bool ok = false;
    const std::string text = read_text_file(current_path_, ok);
    if (!ok) {
        // Readable a moment ago, not now: mid-write, or locked by whatever is
        // writing it.  Leave the buffer alone rather than blanking it.
        SetStatusText(L"Changed on disk, but could not be read just now.");
        return;
    }
    if (first_invalid_utf8(text) != std::string::npos) {
        // Whatever wrote the file damaged it; strict FromUTF8 would turn it
        // into an empty buffer.  Keep the good text we already have.
        SetStatusText(L"Changed on disk, but is no longer valid UTF-8 — "
                      L"not reloaded.");
        return;
    }

    // Keep the reader where they were.  Both are clamped because the file may
    // have shrunk since, and Scintilla is happy to be told about a line that
    // no longer exists.
    const int first_visible = editor_->GetFirstVisibleLine();
    const int caret = editor_->GetCurrentPos();

    editor_->SetText(wxString::FromUTF8(text));
    editor_->EmptyUndoBuffer();
    editor_->GotoPos(std::min(caret, editor_->GetLength()));
    editor_->ScrollToLine(std::min(first_visible,
                                   std::max(0, editor_->GetLineCount() - 1)));

    // SetText raises the change event, which set the dirty flag; the document
    // now matches the file, so it is clean whatever that handler concluded.
    dirty_ = false;
    watcher_.accept_current_state();
    update_title();
    render_preview();
    SetStatusText(L"Reloaded — the file changed on disk.");
}

void MainFrame::on_exit(wxCommandEvent&)
{
    Close(false);
}

void MainFrame::on_toggle_front_matter(wxCommandEvent& event)
{
    // Deliberately NOT event.IsChecked().  The View menu item and the toolbar
    // button are two separate check controls sharing one id, and toggling one
    // never updated the other -- so after a click on the toolbar the menu
    // still showed the old state, and the next click from the menu reported a
    // value that flipped the setting the wrong way.  Reading and inverting the
    // stored setting makes the two agree by construction.
    const bool hide = !config_.hide_front_matter();
    config_.set_hide_front_matter(hide);
    sync_front_matter_checks(hide);
    // Written now rather than at exit.  A setting that only reaches disk on a
    // clean shutdown is lost to any crash, kill or power cut, which reads
    // exactly like "it does not remember what I chose".
    config_.save();
    render_preview();
    event.Skip(false);
}

void MainFrame::sync_front_matter_checks(bool hide)
{
    if (wxMenuBar* menus = GetMenuBar()) {
        menus->Check(kIdToggleFrontMatter, hide);
    }
    if (wxToolBar* bar = GetToolBar()) {
        bar->ToggleTool(kIdToggleFrontMatter, hide);
    }
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
    const std::string markdown = editor_->GetText().utf8_string();
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
    clear_document();
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
    watcher_.watch(current_path_);
    dirty_ = true;
    update_title();
    render_preview();
}

void MainFrame::on_open_templates_folder(wxCommandEvent&)
{
    if (seed_templates(config_)) {
        config_.save();   // a starter this build added was written just now
    }
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
    save_pane_visibility();
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
    save_pane_visibility();
}

void MainFrame::on_toggle_editor(wxCommandEvent&)
{
    if (split_->IsSplit()) {
        hidden_editor_sash_ = split_->GetSashPosition();
        split_->Unsplit(editor_);
    } else {
        split_->SplitVertically(editor_, preview_, hidden_editor_sash_);
    }
    save_pane_visibility();
}

void MainFrame::save_pane_visibility()
{
    // Which panes are showing is recorded the moment it changes, not only in
    // on_close().  Exit still saves the sash positions, which are only worth
    // reading once the window has stopped being resized; visibility is a
    // deliberate choice and should survive however the app happens to end.
    if (files_split_ != nullptr) {
        config_.set_show_files(files_split_->IsSplit());
    }
    if (outline_split_ != nullptr) {
        config_.set_show_outline(outline_split_->IsSplit());
    }
    if (split_ != nullptr) {
        config_.set_show_editor(split_->IsSplit());
    }
    config_.save();
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

void MainFrame::on_check_updates(wxCommandEvent&)
{
    // Only ever started by the user from the menu.  There is no check on
    // launch: an app that phones home unasked is not what this one is for.
    SetStatusText(L"Checking for updates…");
    check_for_update([this](const ReleaseInfo& info, const std::string& error) {
        SetStatusText(wxString());
        if (!error.empty()) {
            wxMessageBox("Could not check for updates.\n\n" +
                             wxString::FromUTF8(error),
                         "MD Boss", wxOK | wxICON_WARNING, this);
            return;
        }
        const std::optional<std::vector<int>> current =
            parse_version(kAppVersion);
        if (info.version.empty() || !current) {
            wxMessageBox("GitHub did not return a version this build "
                         "understands.",
                         "MD Boss", wxOK | wxICON_WARNING, this);
            return;
        }
        if (!is_newer(info.version, *current)) {
            wxMessageBox(wxString("You are up to date (v") + kAppVersion +
                             ").",
                         "MD Boss", wxOK | wxICON_INFORMATION, this);
            return;
        }

        // A loose copy with no uninstaller beside it updates from the
        // portable zip; an installed one from the installer.  Decided here,
        // once, so the download and the hand-off cannot disagree.
        const std::filesystem::path exe = path_from_utf8(std::string(
            wxStandardPaths::Get().GetExecutablePath().ToUTF8()));
        const bool portable = portable_install(
            path_to_utf8(exe.parent_path()));

        // A release may carry the Python assets but not this build's, in
        // which case there is nothing to download -- say so and offer the
        // page rather than failing silently.
        const std::string& url = portable ? info.portable_url
                                          : info.setup_url;
        if (url.empty()) {
            const int answer = wxMessageBox(
                wxString("v") + wxString::FromUTF8(info.version_str) +
                    " is available, but it has no download for this copy.\n\n"
                    "Open the releases page?",
                "MD Boss", wxYES_NO | wxICON_INFORMATION, this);
            if (answer == wxYES) {
                wxLaunchDefaultBrowser(wxString::FromUTF8(info.html_url));
            }
            return;
        }
        const int answer = wxMessageBox(
            wxString("v") + wxString::FromUTF8(info.version_str) +
                " is available (you have v" + kAppVersion + ").\n\n"
                "Download and install it now?\n\n"
                "MD Boss will close, install, and reopen.",
            "MD Boss", wxYES_NO | wxICON_QUESTION, this);
        if (answer == wxYES) {
            install_update(info, portable);
        }
    });
}

void MainFrame::install_update(const ReleaseInfo& info, bool portable)
{
    const std::string& url = portable ? info.portable_url : info.setup_url;
    assert(!url.empty() && "nothing to download");
    // Asked before the download, not after: a user who cancels here has not
    // waited for several megabytes first.
    if (!confirm_discard()) {
        return;
    }

    const std::filesystem::path dest =
        path_from_utf8(std::string(wxStandardPaths::Get()
                                       .GetTempDir()
                                       .ToUTF8())) /
        path_from_utf8(portable ? kPortableAssetName : kSetupAssetName);

    SetStatusText(L"Downloading the update…");
    download_update(
        url, path_to_utf8(dest),
        [this, dest, portable](const std::string& error) {
            SetStatusText(wxString());
            if (!error.empty()) {
                wxMessageBox("Could not download the update.\n\n" +
                                 wxString::FromUTF8(error),
                             "MD Boss", wxOK | wxICON_WARNING, this);
                return;
            }
            if (portable) {
                hand_off_to_portable(path_to_utf8(dest));
            } else {
                hand_off_to_installer(path_to_utf8(dest));
            }
        });
}

void MainFrame::hand_off_to_installer(const std::string& setup_path)
{
    const std::string app_exe =
        std::string(wxStandardPaths::Get().GetExecutablePath().ToUTF8());
    const std::string text = installer_batch(
        setup_path, app_exe,
        static_cast<unsigned long>(::GetCurrentProcessId()));
    spawn_handoff_and_close(setup_path + ".cmd", text);
}

void MainFrame::hand_off_to_portable(const std::string& zip_path)
{
    const std::string app_exe =
        std::string(wxStandardPaths::Get().GetExecutablePath().ToUTF8());
    // Staging lands beside the zip, like app.py's `zip + ".new"`; the batch
    // creates it, copies from it, and removes it.
    const std::string text = portable_batch(
        zip_path, zip_path + ".new", app_exe,
        static_cast<unsigned long>(::GetCurrentProcessId()));
    spawn_handoff_and_close(zip_path + ".cmd", text);
}

void MainFrame::spawn_handoff_and_close(const std::string& batch_path,
                                        const std::string& text)
{
    // Written as ASCII with CRLF: cmd.exe is the reader, and it is fussier
    // than anything else in this program about both.
    {
        std::ofstream stream(path_from_utf8(batch_path),
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            wxMessageBox("Could not prepare the update.", "MD Boss",
                         wxOK | wxICON_WARNING, this);
            return;
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream.good()) {
            wxMessageBox("Could not prepare the update.", "MD Boss",
                         wxOK | wxICON_WARNING, this);
            return;
        }
    }

    // Windowless, in its own process group, and NOT detached -- exactly what
    // the Python app uses, and the difference is not cosmetic.
    //
    // DETACHED_PROCESS was tried and hung the update: it is mutually
    // exclusive with CREATE_NO_WINDOW, and the batch's `tasklist | find`
    // inherited no usable stdin, so find.exe blocked forever reading it and
    // the installer was never reached.  The app had already exited by then,
    // so the update simply did not happen and nothing said why -- the exact
    // failure the wait loop's own comments were written about.
    //
    // CREATE_NO_WINDOW keeps the console hidden; the batch still outlives us
    // because cmd.exe is not tied to this process's lifetime.
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"cmd.exe /c \"" +
                           path_from_utf8(batch_path).wstring() + L"\"";
    const BOOL started = ::CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr, &startup, &process);
    if (!started) {
        wxMessageBox("Could not start the update.", "MD Boss",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    ::CloseHandle(process.hProcess);
    ::CloseHandle(process.hThread);

    // The batch is already waiting for this process to disappear.
    updating_ = true;
    Close(true);
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
    // Held until the timer fires, not cleared on the next line: scroll_to()
    // runs ExecuteScript, which returns before the preview has scrolled, so
    // clearing here would leave the echo unguarded.  That echo maps back
    // through a different document height and lands a line or so off, which
    // is invisible on a fast scroll and looks like a bounce on a slow one.
    suppress_preview_scroll_ = true;
    preview_->scroll_to(ratio);
    // Restarting rather than scheduling a fresh callback per sync means a
    // burst of wheel events stays suppressed until 120 ms after the last.
    scroll_echo_timer_.Start(kScrollEchoMs, wxTIMER_ONE_SHOT);
}

void MainFrame::on_scroll_echo_timer(wxTimerEvent&)
{
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
    // Clamped because a rubber-band overscroll reports outside [0,1], and
    // rounded rather than truncated: truncation always biases towards the
    // top, so the round trip loses a line at a time instead of landing back
    // where it started.  SetFirstVisibleLine's echo IS synchronous, so this
    // guard can wrap the call -- unlike the one in sync_preview_from_editor.
    const double clamped = std::min(1.0, std::max(0.0, ratio));
    suppress_editor_scroll_ = true;
    editor_->SetFirstVisibleLine(
        static_cast<int>(std::lround(clamped * span)));
    suppress_editor_scroll_ = false;
}

std::vector<std::string> MainFrame::root_paths() const
{
    std::vector<std::string> paths;
    paths.reserve(config_.roots().size());
    for (const Root& root : config_.roots()) {
        paths.push_back(root.path);
    }
    return paths;
}

void MainFrame::update_title()
{
    wxString name = "Untitled";
    wxString location;
    if (!current_path_.empty()) {
        name = wxString::FromUTF8(
            path_to_utf8(path_from_utf8(current_path_).filename()));
        // A document opened from outside every root has nothing in the tree
        // pointing at it -- Ctrl+O, a drop, a command line or the Windows file
        // association can all put one here -- so the title carries its full
        // path.  A document that IS in the tree does not need it: the tree
        // already shows where it lives, and the path would only crowd out the
        // name at the front, which is what the taskbar truncates to.
        if (!is_under_any_root(current_path_, root_paths())) {
            location = " - " + wxString::FromUTF8(current_path_);
        }
    }
    SetTitle(wxString(dirty_ ? "*" : "") + name + location + " - MD Boss - v" +
             kAppVersion);
    update_close_enabled();
}

void MainFrame::update_close_enabled()
{
    // Explicitly, rather than through wxEVT_UPDATE_UI: the toolbar never
    // delivered that event to this frame, so the button stayed enabled with
    // nothing open.  Every path that changes what is open already ends in
    // update_title(), which makes this the one place it has to be done.
    //
    // An unsaved buffer with no path still counts as open, or Close would be
    // dead exactly when discarding is what the user wants.
    const bool open = !current_path_.empty() ||
                      (editor_ != nullptr && editor_->GetLength() > 0);
    if (wxToolBar* bar = GetToolBar()) {
        bar->EnableTool(kIdCloseDocument, open);
    }
    if (wxMenuBar* menus = GetMenuBar()) {
        menus->Enable(kIdCloseDocument, open);
    }
}

void MainFrame::clear_document()
{
    editor_->SetText("");
    editor_->EmptyUndoBuffer();
    current_path_.clear();
    watcher_.watch(current_path_);
    dirty_ = false;
    // Any warning on show belonged to the document being cleared.
    SetStatusText(wxString());
    update_title();
    render_preview();
}

void MainFrame::on_close_document(wxCommandEvent&)
{
    if (!confirm_discard()) {
        return;
    }
    clear_document();
}

void MainFrame::insert_block(const std::string& body)
{
    assert(editor_ != nullptr && "a snippet needs somewhere to go");
    assert(!body.empty() && "an empty block is not worth inserting");

    // Every one of these is a block construct -- an alert, a fence, a table,
    // a figure -- and a block only renders as one when it starts its own line
    // and is followed by a blank one.  Dropped mid-sentence it would be swept
    // into the surrounding paragraph and come out as literal text, so the gaps
    // are made here rather than left to the user to notice afterwards.
    const int position = editor_->GetCurrentPos();
    const int length = editor_->GetLength();

    const bool at_line_start =
        position == 0 || editor_->GetCharAt(position - 1) == '\n';
    std::string lead;
    if (!at_line_start) {
        lead = "\n\n";
    } else if (position > 1 && editor_->GetCharAt(position - 2) != '\n') {
        lead = "\n";   // on a fresh line, but the one above has content
    }

    std::string block = body;
    if (block.empty() || block.back() != '\n') {
        block += '\n';
    }

    // A blank line after matters as much as one before, and for a table it is
    // the difference between a paragraph and another row: text typed straight
    // after "|  |  |" is parsed as one.  Skipped when the next line is
    // already blank, or when there is nothing after this at all.
    std::string tail;
    if (position < length && editor_->GetCharAt(position) != '\n') {
        tail = "\n";
    }

    editor_->InsertText(position, wxString::FromUTF8(lead + block + tail));
    // On the line immediately after the block -- the blank one, when there is
    // one -- rather than stranded at the start of what the user is about to
    // replace.
    editor_->GotoPos(position + static_cast<int>(lead.size() + block.size()));
    editor_->SetFocus();
}

void MainFrame::on_snippet(wxCommandEvent& event)
{
    const int index = event.GetId() - kIdSnippetBase;
    if (index < 0 || static_cast<std::size_t>(index) >= std::size(kSnippets)) {
        return;   // an id outside the range this handler was bound for
    }
    insert_block(kSnippets[static_cast<std::size_t>(index)].body);
}

void MainFrame::on_insert_image(wxCommandEvent&)
{
    // Opening in the document's own folder is the difference between picking
    // the image next to the note and hunting for it: figures nearly always
    // live beside the document that shows them.
    const wxString start =
        current_path_.empty()
            ? wxString()
            : wxString::FromUTF8(path_to_utf8(
                  path_from_utf8(current_path_).parent_path()));

    wxFileDialog dialog(this, L"Insert image file", start, "", kImageWildcard,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    const std::string chosen = std::string(dialog.GetPath().ToUTF8());
    const std::string link = markdown_image_link(chosen, current_path_);
    if (link.empty()) {
        return;
    }
    insert_block(link);

    // Worth saying out loud: an absolute path breaks the moment the document
    // is sent anywhere.  Decided from the paths rather than by sniffing the
    // link text -- the same two conditions markdown_image_link() uses.
    if (current_path_.empty()) {
        SetStatusText(L"Absolute path used — the document has not been saved "
                      L"anywhere yet.");
    } else if (path_from_utf8(chosen).root_name() !=
               path_from_utf8(current_path_).root_name()) {
        SetStatusText(L"Absolute path used — the image is on a different "
                      L"drive from the document.");
    }
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
    // install_update() already asked, and the installer is waiting on this
    // process to exit -- asking again here would stall the handoff behind a
    // dialog the user has effectively already answered.
    if (!updating_ && event.CanVeto() && !confirm_discard()) {
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
    if (files_ != nullptr) {
        // Which folders were open is part of the layout: reopening collapsed
        // means clicking back down to the same folder every launch.
        config_.set_expanded_folders(files_->expanded_folders());
    }
    config_.save();
    event.Skip();
}

}  // namespace mdboss

