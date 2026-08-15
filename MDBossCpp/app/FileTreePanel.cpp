#include "FileTreePanel.h"

#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

#include <wx/app.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "FileScan.h"
#include "PathUtf8.h"
#include "Templates.h"

namespace mdboss {
namespace {

// Bound on how deep a filtered search will walk (Rule of 10).
constexpr int kMaxFilterDepth = 24;

// How long the content search waits for typing to stop.  Long enough that a
// word typed at speed starts one search rather than one per letter, short
// enough not to feel like a pause.
constexpr int kSearchDebounceMs = 350;

// Non-ASCII UI text must be a WIDE literal.  A narrow "…" in a UTF-8 source
// is handed to wxString as bytes and decoded in the current ANSI codepage,
// which renders as "â€¦".  Wide literals are unambiguous.  mojibake-ok: that
// example is meant to look broken; see the encoding guard in test_sources.
const wchar_t* const kLazyPlaceholder = L"…";

// Per-item payload: the path, and whether it is a file we can open.
class ItemData : public wxTreeItemData {
public:
    ItemData(std::string path, bool is_file)
        : path_(std::move(path)), is_file_(is_file)
    {
    }
    const std::string& path() const { return path_; }
    bool is_file() const { return is_file_; }

private:
    std::string path_;
    bool is_file_ = false;
};

constexpr int kIdOpen = wxID_HIGHEST + 60;
constexpr int kIdNewFile = wxID_HIGHEST + 61;
constexpr int kIdNewFolder = wxID_HIGHEST + 62;
constexpr int kIdRename = wxID_HIGHEST + 63;
constexpr int kIdDelete = wxID_HIGHEST + 64;
constexpr int kIdReveal = wxID_HIGHEST + 65;
constexpr int kIdCopyPath = wxID_HIGHEST + 66;
constexpr int kIdFavorite = wxID_HIGHEST + 67;
constexpr int kIdImportInbox = wxID_HIGHEST + 68;
constexpr int kIdManageTemplates = wxID_HIGHEST + 69;
constexpr int kIdManageFolders = wxID_HIGHEST + 70;
constexpr int kIdFlatList = wxID_HIGHEST + 71;
// Templates take ids from here up.  Bounded (Rule of 10) so a folder full of
// templates cannot run the range into the next constant.
constexpr int kIdTemplateBase = wxID_HIGHEST + 80;
constexpr std::size_t kMaxTemplatesShown = 20;

// The body of a new document: the blank heading, or a template with its
// placeholders substituted.  A template that cannot be read warns and falls
// back to the blank body rather than failing the creation, as Python's
// _template_content() does -- the user asked for a file, not for a template.
std::string body_for_new_document(const std::string& template_path,
                                  const std::string& title, wxWindow* parent)
{
    if (template_path.empty()) {
        return "# " + title + "\n\n";
    }
    std::ifstream stream(path_from_utf8(template_path), std::ios::binary);
    if (!stream) {
        wxMessageBox("Cannot read template:\n" +
                         wxString::FromUTF8(template_path),
                     "MD Boss", wxOK | wxICON_WARNING, parent);
        return "# " + title + "\n\n";
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return apply_template(strip_utf8_bom(buffer.str()), title);
}

std::string lowered(const std::string& text)
{
    std::string out = text;
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace

FileTreePanel::FileTreePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    filter_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                             wxDefaultSize, wxTE_PROCESS_ENTER);
    filter_->SetHint(L"Filter files…");
    contents_ = new wxCheckBox(this, wxID_ANY, L"Contents");
    contents_->SetToolTip(
        L"Also match text inside files, not just their names.  Searching "
        L"reads every Markdown file under each root, so it runs in the "
        L"background and needs at least two characters.");
    tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT |
                               wxTR_LINES_AT_ROOT);

    // The box and its modifier on one row: the checkbox changes what the text
    // beside it means, so putting it anywhere else would hide that.
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    top->Add(filter_, 1, wxEXPAND | wxALL, 2);
    top->Add(contents_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(top, 0, wxEXPAND);
    sizer->Add(tree_, 1, wxEXPAND);
    SetSizer(sizer);

    filter_->Bind(wxEVT_TEXT, &FileTreePanel::on_filter, this);
    contents_->Bind(wxEVT_CHECKBOX, &FileTreePanel::on_filter, this);
    search_timer_.SetOwner(this);
    Bind(wxEVT_TIMER, &FileTreePanel::on_search_timer, this);
    tree_->Bind(wxEVT_TREE_ITEM_EXPANDING, &FileTreePanel::on_expanding, this);
    tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, &FileTreePanel::on_activated, this);
    tree_->Bind(wxEVT_LEFT_DOWN, &FileTreePanel::on_left_click, this);
    tree_->Bind(wxEVT_TREE_ITEM_MENU, &FileTreePanel::on_context_menu, this);
}

FileTreePanel::~FileTreePanel()
{
    // Runs on the UI thread, as does the completion lambda, so a scan that is
    // still in flight can never touch a destroyed panel: either the lambda
    // already ran, or it will see this flag.
    if (alive_) {
        alive_->store(false);
    }
}

void FileTreePanel::set_roots(const std::vector<Root>& roots)
{
    roots_ = roots;
    counts_.clear();
    rebuild();       // show the roots straight away, counts to follow
    start_scan();
}

void FileTreePanel::start_scan()
{
    // Counting must not run on the UI thread.  It did once, in set_roots(),
    // and three real root folders were enough to stall the message loop past
    // the point where WebView2's asynchronous controller creation gives up --
    // CreateCoreWebView2Controller returned E_ABORT and the preview silently
    // never appeared.  The tree is cheap to show without counts; the counts
    // arrive when they arrive.
    const std::vector<Root> roots = roots_;
    if (roots.empty()) {
        return;
    }
    if (!alive_) {
        alive_ = std::make_shared<std::atomic<bool>>(true);
    }
    const unsigned generation = ++scan_generation_;
    std::shared_ptr<std::atomic<bool>> alive = alive_;

    std::thread([this, roots, generation, alive] {
        std::map<std::string, int> counts;
        // Nothing may escape a detached thread: an uncaught exception here is
        // std::terminate, which is a hard crash with no message.
        //
        // The guard is per root, not around the loop. Wrapping the whole loop
        // meant one unreadable root discarded every other root's counts too --
        // a network folder that was briefly unavailable made every tree in the
        // window read (0).
        for (const Root& root : roots) {
            try {
                const std::map<std::string, int> one =
                    md_counts_for_root(root.path);
                counts.insert(one.begin(), one.end());
            } catch (...) {
                // This root contributes nothing; the others still count.
            }
        }
        wxTheApp->CallAfter([this, counts, generation, alive] {
            if (!alive->load() || generation != scan_generation_) {
                return;   // panel gone, or a newer scan already superseded us
            }
            counts_ = counts;
            rebuild_preserving_expansion();
        });
    }).detach();
}

void FileTreePanel::rebuild()
{
    tree_->DeleteAllItems();
    const wxTreeItemId hidden = tree_->AddRoot("roots");
    const std::string needle = lowered(std::string(filter_->GetValue().ToUTF8()));

    for (const Root& root : roots_) {
        const auto found = counts_.find(norm_path(root.path));
        const int count = (found == counts_.end()) ? 0 : found->second;
        const wxString label =
            wxString::FromUTF8(root.name) +
            wxString::Format("  (%d)", count);
        const wxTreeItemId item = tree_->AppendItem(hidden, label);
        tree_->SetItemData(item, new ItemData(root.path, false));
        tree_->SetItemBold(item, true);

        // A flat root, or any root while a filter is active, shows its
        // Markdown files directly under it rather than as a folder tree --
        // populate_filtered with an empty needle matches every file.  Useful
        // for a deep folder structure that holds only a handful of documents.
        const bool flat = is_flat_folder_ && is_flat_folder_(root.path);
        const bool searching = !content_query().empty();
        if (needle.empty() && !flat && !searching) {
            // Lazily populated: a placeholder makes the expander appear.
            tree_->AppendItem(item, kLazyPlaceholder);
        } else {
            populate_filtered(item, root.path, needle, 0);
            if (searching) {
                append_content_hits(item, root.path);
            }
            tree_->Expand(item);
        }
    }
}

void FileTreePanel::append_content_hits(const wxTreeItemId& item,
                                        const std::string& root)
{
    // The name filter has already listed whatever matched by name; these are
    // the files that matched only on their text.  Anything already shown is
    // skipped so a file matching both ways appears once.
    std::vector<std::string> shown;
    wxTreeItemIdValue cookie;
    for (wxTreeItemId child = tree_->GetFirstChild(item, cookie);
         child.IsOk(); child = tree_->GetNextChild(item, cookie)) {
        auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(child));
        if (data != nullptr) {
            shown.push_back(norm_path(data->path()));
        }
    }

    if (searching_) {
        tree_->AppendItem(item, L"searching…");
        return;
    }
    const auto found = content_hits_.find(root);
    if (found == content_hits_.end()) {
        return;
    }

    for (const ContentMatch& match : found->second) {   // bounded by the cap
        const std::string key = norm_path(match.path);
        bool already = false;
        for (const std::string& seen : shown) {
            if (seen == key) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        const std::string name =
            path_to_utf8(path_from_utf8(match.path).filename());
        const wxTreeItemId file =
            tree_->AppendItem(item, wxString::FromUTF8(name));
        tree_->SetItemData(file, new ItemData(match.path, true));

        // The matching line as a child, so you can see why the file matched
        // without opening it.  It carries the file's path too: clicking the
        // reason should open the thing it is a reason for.
        const wxTreeItemId hit = tree_->AppendItem(
            file, wxString::Format("%d: ", match.line) +
                      wxString::FromUTF8(match.text));
        tree_->SetItemData(hit, new ItemData(match.path, true));
        tree_->Expand(file);
    }
}

bool FileTreePanel::is_root_path(const std::string& path) const
{
    // Exact compare: a root's path in the tree is the same string the config
    // entry holds, both having come from there.
    for (const Root& root : roots_) {   // bounded by the configured roots
        if (root.path == path) {
            return true;
        }
    }
    return false;
}

void FileTreePanel::populate(const wxTreeItemId& item, const std::string& path)
{
    // A flattened folder shows every Markdown file beneath it and no
    // subfolders at all.  populate_filtered with an empty needle matches every
    // file, which is exactly that.  Dispatching here rather than at the call
    // sites means expanding one, restoring one, and rebuilding one all agree.
    if (is_flat_folder_ && is_flat_folder_(path)) {
        populate_filtered(item, path, "", 0);
        return;
    }
    for (const Entry& entry : list_directory(path)) {
        if (entry.is_dir) {
            // Counts are recursive, so 0 means nothing is being concealed.
            // A folder absent from the map was never walked (junction or
            // unreadable) -- leave it visible rather than guess.
            const auto found = counts_.find(norm_path(entry.path));
            if (found != counts_.end() && found->second == 0) {
                continue;
            }
            const int count = (found == counts_.end()) ? 0 : found->second;
            const wxTreeItemId child = tree_->AppendItem(
                item, wxString::FromUTF8(entry.name) +
                          wxString::Format("  (%d)", count));
            tree_->SetItemData(child, new ItemData(entry.path, false));
            tree_->AppendItem(child, kLazyPlaceholder);
        } else {
            const wxTreeItemId child =
                tree_->AppendItem(item, wxString::FromUTF8(entry.name));
            tree_->SetItemData(child, new ItemData(entry.path, true));
        }
    }
}

void FileTreePanel::populate_filtered(const wxTreeItemId& root_item,
                                      const std::string& path,
                                      const std::string& needle, int depth)
{
    if (depth > kMaxFilterDepth) {
        return;
    }
    for (const Entry& entry : list_directory(path)) {
        if (entry.is_dir) {
            const auto found = counts_.find(norm_path(entry.path));
            if (found != counts_.end() && found->second == 0) {
                continue;
            }
            populate_filtered(root_item, entry.path, needle, depth + 1);
        } else if (lowered(entry.name).find(needle) != std::string::npos) {
            const wxTreeItemId child =
                tree_->AppendItem(root_item, wxString::FromUTF8(entry.name));
            tree_->SetItemData(child, new ItemData(entry.path, true));
        }
    }
}

void FileTreePanel::on_expanding(wxTreeEvent& event)
{
    const wxTreeItemId item = event.GetItem();
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
    if (data == nullptr || data->is_file()) {
        return;
    }
    // A single "…" child means this folder has not been read yet.
    wxTreeItemIdValue cookie;
    const wxTreeItemId first = tree_->GetFirstChild(item, cookie);
    if (!first.IsOk() || tree_->GetItemText(first) != kLazyPlaceholder) {
        return;
    }
    tree_->DeleteChildren(item);
    populate(item, data->path());
}

void FileTreePanel::on_activated(wxTreeEvent& event)
{
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (data != nullptr && data->is_file() && on_open_) {
        on_open_(data->path());
    }
}

void FileTreePanel::on_left_click(wxMouseEvent& event)
{
    // A single click opens a file; Enter and double-click still route through
    // on_activated.  Hit-test the click point rather than trusting the current
    // selection -- the click may land on a row that is not selected yet, and
    // that row is the one to open.  Keyboard navigation does NOT come through
    // here, so arrowing down the tree only selects (and never fires the
    // unsaved-edits prompt that opening would).
    int flags = 0;
    const wxTreeItemId item = tree_->HitTest(event.GetPosition(), flags);
    if (item.IsOk()) {
        auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
        if (data != nullptr && data->is_file() && on_open_) {
            on_open_(data->path());
        }
    }
    event.Skip();   // let the tree select and focus the row as usual
}

void FileTreePanel::on_filter(wxCommandEvent& event)
{
    // Redraw on the name filter straight away -- that costs a directory
    // listing -- and let the content search catch up behind it.
    rebuild();
    if (content_query().empty()) {
        content_hits_.clear();
        searched_for_.clear();
        searching_ = false;
        search_timer_.Stop();
    } else {
        // Coalesce keystrokes.  Without this every letter of a five-letter
        // word starts a full-tree read, and four of the five are wasted.
        search_timer_.Start(kSearchDebounceMs, wxTIMER_ONE_SHOT);
    }
    event.Skip();
}

std::string FileTreePanel::content_query() const
{
    if (contents_ == nullptr || !contents_->GetValue()) {
        return {};
    }
    const std::string text = lowered(std::string(filter_->GetValue().ToUTF8()));
    if (text.size() < kMinSearchNeedle) {
        return {};
    }
    return text;
}

void FileTreePanel::on_search_timer(wxTimerEvent&)
{
    start_content_search();
}

void FileTreePanel::start_content_search()
{
    const std::string needle = content_query();
    if (needle.empty() || roots_.empty()) {
        return;
    }
    if (!alive_) {
        alive_ = std::make_shared<std::atomic<bool>>(true);
    }
    const unsigned generation = ++search_generation_;
    const std::vector<Root> roots = roots_;
    std::shared_ptr<std::atomic<bool>> alive = alive_;

    searching_ = true;
    rebuild();   // show "searching…" while the worker runs

    std::thread([this, roots, needle, generation, alive] {
        std::map<std::string, std::vector<ContentMatch>> hits;
        for (const Root& root : roots) {   // bounded by the roots list
            if (!alive->load() || generation != search_generation_.load()) {
                return;   // the query moved on; the answer is worthless
            }
            // Nothing may escape a detached thread: an uncaught exception is
            // std::terminate, a hard crash with no message.  Guarded per root
            // rather than around the loop, for the reason start_scan()
            // records -- one unreadable root must not discard the rest.
            try {
                hits[root.path] = search_file_contents(
                    root.path, needle, [this, generation, alive] {
                        return !alive->load() ||
                               generation != search_generation_.load();
                    });
            } catch (...) {
                // This root contributes nothing; the others still search.
            }
        }
        wxTheApp->CallAfter([this, hits, needle, generation, alive] {
            if (!alive->load() || generation != search_generation_.load()) {
                return;
            }
            content_hits_ = hits;
            searched_for_ = needle;
            searching_ = false;
            rebuild();
        });
    }).detach();
}

// ----------------------------------------------------------- refreshing --

void FileTreePanel::refresh()
{
    // Show the change at once using the counts we have, then re-scan in the
    // background to correct them.  Doing it the other way round would leave
    // a renamed file looking untouched until the scan finished.
    rebuild_preserving_expansion();
    start_scan();
}

void FileTreePanel::rebuild_preserving_expansion()
{
    // Rebuilding blind would collapse the whole tree, so remember what was
    // open and put it back.
    std::vector<std::string> expanded;
    const wxTreeItemId hidden = tree_->GetRootItem();
    if (hidden.IsOk()) {
        wxTreeItemIdValue cookie;
        for (wxTreeItemId root = tree_->GetFirstChild(hidden, cookie);
             root.IsOk(); root = tree_->GetNextChild(hidden, cookie)) {
            collect_expanded(root, expanded, 0);
        }
    }

    rebuild();

    const wxTreeItemId new_hidden = tree_->GetRootItem();
    if (new_hidden.IsOk()) {
        wxTreeItemIdValue cookie;
        for (wxTreeItemId root = tree_->GetFirstChild(new_hidden, cookie);
             root.IsOk(); root = tree_->GetNextChild(new_hidden, cookie)) {
            restore_expanded(root, expanded, 0);
        }
    }
}

void FileTreePanel::collect_expanded(const wxTreeItemId& item,
                                     std::vector<std::string>& out,
                                     int depth) const
{
    if (depth > kMaxFilterDepth || !item.IsOk() || !tree_->IsExpanded(item)) {
        return;
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
    if (data != nullptr && !data->is_file()) {
        out.push_back(norm_path(data->path()));
    }
    wxTreeItemIdValue cookie;
    for (wxTreeItemId child = tree_->GetFirstChild(item, cookie);
         child.IsOk(); child = tree_->GetNextChild(item, cookie)) {
        collect_expanded(child, out, depth + 1);
    }
}

void FileTreePanel::restore_expanded(const wxTreeItemId& item,
                                     const std::vector<std::string>& paths,
                                     int depth)
{
    if (depth > kMaxFilterDepth || !item.IsOk()) {
        return;
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
    if (data == nullptr || data->is_file()) {
        return;
    }
    // A flat ROOT was already fully populated and expanded by rebuild(), so
    // there is nothing to restore and re-listing it would only throw the same
    // files away and read them again.  A flat SUBFOLDER is not exempt: it sits
    // behind a lazy placeholder like any other, and populate() now knows to
    // give it a flat listing.
    if (is_flat_folder_ && is_flat_folder_(data->path()) &&
        is_root_path(data->path())) {
        return;
    }
    const std::string key = norm_path(data->path());
    bool wanted = false;
    for (const std::string& path : paths) {
        if (path == key) {
            wanted = true;
            break;
        }
    }
    if (!wanted) {
        return;
    }
    tree_->DeleteChildren(item);
    populate(item, data->path());
    tree_->Expand(item);

    wxTreeItemIdValue cookie;
    for (wxTreeItemId child = tree_->GetFirstChild(item, cookie);
         child.IsOk(); child = tree_->GetNextChild(item, cookie)) {
        restore_expanded(child, paths, depth + 1);
    }
}

wxTreeItemId FileTreePanel::find_item(const std::string& path) const
{
    const wxTreeItemId hidden = tree_->GetRootItem();
    if (!hidden.IsOk() || path.empty()) {
        return wxTreeItemId();
    }
    const std::string key = norm_path(path);
    wxTreeItemIdValue cookie;
    for (wxTreeItemId root = tree_->GetFirstChild(hidden, cookie);
         root.IsOk(); root = tree_->GetNextChild(hidden, cookie)) {
        const wxTreeItemId found = find_item_under(root, key, 0);
        if (found.IsOk()) {
            return found;
        }
    }
    return wxTreeItemId();
}

wxTreeItemId FileTreePanel::find_item_under(const wxTreeItemId& item,
                                            const std::string& key,
                                            int depth) const
{
    if (depth > kMaxFilterDepth || !item.IsOk()) {
        return wxTreeItemId();
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
    if (data != nullptr && norm_path(data->path()) == key) {
        return item;
    }
    // Only what is already built is searched -- an unexpanded folder holds a
    // placeholder, and reading the disk to answer "where was I" would undo the
    // laziness the whole tree is built on.
    wxTreeItemIdValue cookie;
    for (wxTreeItemId child = tree_->GetFirstChild(item, cookie);
         child.IsOk(); child = tree_->GetNextChild(item, cookie)) {
        const wxTreeItemId found = find_item_under(child, key, depth + 1);
        if (found.IsOk()) {
            return found;
        }
    }
    return wxTreeItemId();
}

// -------------------------------------------------------- context menu --

void FileTreePanel::on_context_menu(wxTreeEvent& event)
{
    const wxTreeItemId item = event.GetItem();
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
    if (data == nullptr) {
        return;
    }
    tree_->SelectItem(item);

    const std::string path = data->path();
    const bool is_file = data->is_file();
    // New items land beside a file, or inside a folder.
    const std::string dir =
        is_file ? path_to_utf8(path_from_utf8(path).parent_path()) : path;

    wxMenu menu;
    if (is_file) {
        menu.Append(kIdOpen, "&Open");
        menu.AppendSeparator();
    }
    // "New file" is a submenu rather than one item so a document can be
    // started from a template *in the folder you clicked*.  Reaching the
    // templates only from the File menu means always creating in the default
    // place and moving the file afterwards.
    const std::vector<std::pair<std::string, std::string>> templates =
        list_templates();
    auto* new_menu = new wxMenu();
    new_menu->Append(kIdNewFile, "&Blank");
    const std::size_t shown =
        std::min<std::size_t>(templates.size(), kMaxTemplatesShown);
    for (std::size_t i = 0; i < shown; ++i) {   // bounded (Rule of 10)
        new_menu->Append(kIdTemplateBase + static_cast<int>(i),
                         wxString::FromUTF8(templates[i].first));
    }
    if (on_manage_templates_) {
        new_menu->AppendSeparator();
        new_menu->Append(kIdManageTemplates, L"&Manage templates…");
    }
    menu.AppendSubMenu(new_menu, "&New file");
    menu.Append(kIdNewFolder, L"New &folder…");
    menu.AppendSeparator();
    menu.Append(kIdRename, L"Re&name…");
    menu.Append(kIdDelete, "&Delete");
    menu.AppendSeparator();
    menu.Append(kIdReveal, "Reveal in &Explorer");
    menu.Append(kIdCopyPath, "&Copy path");
    if (is_file && on_toggle_favorite_) {
        const bool favorite = is_favorite_ && is_favorite_(path);
        menu.Append(kIdFavorite,
                    favorite ? "Remove from fa&vorites" : "Add to fa&vorites");
    }
    // Offered on any folder, not just a top-level root: a deep subtree holding
    // a handful of documents is exactly as tedious to click through whether it
    // starts at a root or three levels down.
    if (!is_file && on_toggle_flat_) {
        menu.AppendSeparator();
        wxMenuItem* flat = menu.AppendCheckItem(kIdFlatList,
                                                "Show as flat &list");
        flat->Check(is_flat_folder_ && is_flat_folder_(path));
    }
    if (on_import_to_inbox_ || on_manage_folders_) {
        menu.AppendSeparator();
    }
    if (on_import_to_inbox_) {
        menu.Append(kIdImportInbox,
                    wxString(L"&Import files into ") + kInboxName + L"…");
    }
    if (on_manage_folders_) {
        menu.Append(kIdManageFolders, L"Manage f&olders…");
    }

    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_open_) {
            on_open_(path);
        }
    }, kIdOpen);
    menu.Bind(wxEVT_MENU, [this, dir](wxCommandEvent&) {
        new_document(dir, std::string());
    }, kIdNewFile);
    for (std::size_t i = 0; i < shown; ++i) {
        // The path is copied into the handler rather than the index: the
        // template list is rebuilt on every menu, and binding an index would
        // read whichever list exists when the item is clicked.
        const std::string template_path = templates[i].second;
        menu.Bind(wxEVT_MENU, [this, dir, template_path](wxCommandEvent&) {
            new_document(dir, template_path);
        }, kIdTemplateBase + static_cast<int>(i));
    }
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (on_manage_templates_) {
            on_manage_templates_();
        }
    }, kIdManageTemplates);
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (on_manage_folders_) {
            on_manage_folders_();
        }
    }, kIdManageFolders);
    menu.Bind(wxEVT_MENU, [this, dir](wxCommandEvent&) { new_folder(dir); },
              kIdNewFolder);
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) { rename_path(path); },
              kIdRename);
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) { delete_path(path); },
              kIdDelete);
    menu.Bind(wxEVT_MENU, [path](wxCommandEvent&) {
        // The path must be quoted: unquoted, a space silently opens the
        // wrong folder.
        wxExecute("explorer.exe /select,\"" + wxString::FromUTF8(path) + "\"",
                  wxEXEC_ASYNC);
    }, kIdReveal);
    menu.Bind(wxEVT_MENU, [path](wxCommandEvent&) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(
                new wxTextDataObject(wxString::FromUTF8(path)));
            wxTheClipboard->Close();
        }
    }, kIdCopyPath);
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_toggle_favorite_) {
            on_toggle_favorite_(path);
        }
    }, kIdFavorite);
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_toggle_flat_) {
            on_toggle_flat_(path);   // flips and persists
        }
        // With a filter or a content search running there is no folder tree to
        // stay on -- every root already shows a flat list of matching files --
        // so redraw plainly.  (rebuild_preserving_expansion() would re-list the
        // roots as folders and drop the filtered view.)
        if (!filter_->GetValue().IsEmpty() || !content_query().empty()) {
            rebuild();
            return;
        }
        // Otherwise redraw in the new shape *without* throwing the user back to
        // the start view.  A plain rebuild() collapses every root to its lazy
        // placeholder, so flattening a subfolder three levels down left the
        // folder just toggled off screen entirely -- the one thing the user was
        // looking at.
        rebuild_preserving_expansion();
        const wxTreeItemId item = find_item(path);
        if (item.IsOk()) {
            // Expand as well as select: the whole point of the command is to
            // see the flat listing, and the folder may have been collapsed
            // when the menu was raised.
            tree_->Expand(item);
            tree_->SelectItem(item);
            tree_->EnsureVisible(item);
        }
    }, kIdFlatList);
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (on_import_to_inbox_) {
            on_import_to_inbox_();
        }
    }, kIdImportInbox);

    PopupMenu(&menu);
}

void FileTreePanel::new_document(const std::string& dir,
                                 const std::string& template_path)
{
    const wxString name = wxGetTextFromUser(
        "Name for the new document:", "MD Boss", "untitled.md", this);
    if (name.IsEmpty()) {
        return;
    }
    const wxString filename = wxString::FromUTF8(
        ensure_markdown_extension(std::string(name.ToUTF8())));
    const std::filesystem::path target =
        path_from_utf8(dir) / path_from_utf8(std::string(filename.ToUTF8()));

    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        wxMessageBox("That name is already taken.", "MD Boss",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    const std::string title =
        path_to_utf8(path_from_utf8(std::string(filename.ToUTF8())).stem());
    // This path knows the folder up front, unlike File > New from template, so
    // a TechNote's logo is localized before the file is ever written -- it
    // never touches disk carrying the inline copy.
    const std::string body = localize_embedded_logo(
        body_for_new_document(template_path, title, this),
        path_to_utf8(target));
    const std::string error =
        write_text_file_checked(path_to_utf8(target), body);
    if (!error.empty()) {
        wxMessageBox("Could not create the document:\n" +
                         wxString::FromUTF8(error),
                     "MD Boss", wxOK | wxICON_ERROR, this);
        return;
    }

    refresh();
    if (on_open_) {
        on_open_(path_to_utf8(target));
    }
}

void FileTreePanel::new_folder(const std::string& dir)
{
    const wxString name = wxGetTextFromUser("Name for the new folder:",
                                            "MD Boss", "", this);
    if (name.IsEmpty()) {
        return;
    }
    const std::filesystem::path target =
        path_from_utf8(dir) / path_from_utf8(std::string(name.ToUTF8()));
    std::error_code ec;
    if (!std::filesystem::create_directory(target, ec) || ec) {
        wxMessageBox("Could not create the folder.", "MD Boss",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    // A brand new folder holds no Markdown, so the hiding rule would omit it.
    // Show it anyway by seeding a count the scan will correct on the next
    // refresh -- otherwise the folder the user just made appears not to exist.
    counts_[norm_path(path_to_utf8(target))] = 0;
    refresh();
}

void FileTreePanel::rename_path(const std::string& path)
{
    const std::filesystem::path source = path_from_utf8(path);
    const wxString current = wxString::FromUTF8(path_to_utf8(source.filename()));
    const wxString name =
        wxGetTextFromUser("New name:", "MD Boss", current, this);
    if (name.IsEmpty() || name == current) {
        return;
    }
    const std::filesystem::path target =
        source.parent_path() / path_from_utf8(std::string(name.ToUTF8()));

    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        wxMessageBox("That name is already taken.", "MD Boss",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    std::filesystem::rename(source, target, ec);
    if (ec) {
        wxMessageBox("Could not rename:\n" + wxString::FromUTF8(ec.message()),
                     "MD Boss", wxOK | wxICON_ERROR, this);
        return;
    }
    refresh();
}

void FileTreePanel::delete_path(const std::string& path)
{
    const wxString name =
        wxString::FromUTF8(path_to_utf8(path_from_utf8(path).filename()));
    const int answer = wxMessageBox(
        "Send to the Recycle Bin?\n\n" + name, "MD Boss",
        wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (answer != wxYES) {
        return;
    }
    if (!send_to_recycle_bin(path)) {
        wxMessageBox("Could not delete:\n" + name, "MD Boss",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    refresh();
}

}  // namespace mdboss
