#include "FileTreePanel.h"

#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

#include <wx/app.h>

// WM_HSCROLL/SB_LEFT, to undo EnsureVisible's sideways scroll.  NOMINMAX
// first: without it windows.h defines min/max as macros and the std::min
// call further down stops compiling.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

// Appended to a folder row that is showing as a flat list.
//
// Non-ASCII UI text must be a WIDE literal.  A narrow "…" in a UTF-8 source is
// handed to wxString as bytes and decoded in the current ANSI codepage, which
// renders as "â€¦".  Wide literals are unambiguous.  mojibake-ok: that example
// is meant to look broken; see the encoding guard in test_sources.
const wchar_t* const kFlatSuffix = L"  (flat)";

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
constexpr int kIdExclude = wxID_HIGHEST + 72;
constexpr int kIdPromoteTechNote = wxID_HIGHEST + 73;
constexpr int kSearchTimerId = wxID_HIGHEST + 74;
constexpr int kSelectTimerId = wxID_HIGHEST + 75;

// How long a row must stay selected before it is shown.  Long enough that
// holding an arrow key down scrolls the list rather than opening every file it
// passes -- each open reads the file and re-renders the preview -- and short
// enough that stopping on a row feels like it opened straight away.
constexpr int kSelectDebounceMs = 200;
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

// `path` relative to `root`, '/'-separated -- the form folder_item() splits
// into nodes.  Empty when `path` is not strictly beneath `root`, which is also
// what a caller iterating the roots uses to mean "not this one".
//
// Compared on the normalised form so case and separators cannot cause a miss,
// but the slice is taken from the ORIGINAL string: the tree shows folder names
// as they are spelled on disk, and returning the lower-cased form would rename
// every excluded folder on screen.
std::string relative_under(const std::string& root, const std::string& path)
{
    const std::string norm_root = norm_path(root);
    const std::string norm_full = norm_path(path);
    if (norm_root.empty() || norm_full.size() <= norm_root.size() + 1) {
        return {};
    }
    if (norm_full.compare(0, norm_root.size(), norm_root) != 0) {
        return {};
    }
    const char separator = norm_full[norm_root.size()];
    if (separator != '\\' && separator != '/') {
        return {};   // "C:\Docs" must not match "C:\Docs2"
    }
    // The normalised form differs from the original only in case and in
    // separator spelling, never in length, so the offset carries across --
    // but only take the slice if the original really is that long.
    if (path.size() <= norm_root.size() + 1) {
        return {};
    }
    std::string relative = path.substr(norm_root.size() + 1);
    for (char& ch : relative) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    return relative;
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
    // The flags are spelled out rather than taken from wxTR_DEFAULT_STYLE,
    // which on MSW is wxTR_HAS_BUTTONS | wxTR_NO_LINES | wxTR_LINES_AT_ROOT |
    // wxTR_TWIST_BUTTONS | wxTR_FULL_ROW_HIGHLIGHT -- NO_LINES with
    // TWIST_BUTTONS is what gives the Explorer chevrons and drops the
    // connecting lines.  A folder tree reads better with the lines: they show
    // which parent a deep row belongs to, which chevrons leave you counting
    // indents.  Same set as PDF_Sherpa's PdfListPane, deliberately.
    tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE |
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
    // Each timer gets its own id.  A Bind with no id catches EVERY timer on
    // this window, so adding a second one without ids would have routed its
    // ticks into the search handler.
    search_timer_.SetOwner(this, kSearchTimerId);
    Bind(wxEVT_TIMER, &FileTreePanel::on_search_timer, this, kSearchTimerId);
    select_timer_.SetOwner(this, kSelectTimerId);
    Bind(wxEVT_TIMER, &FileTreePanel::on_select_timer, this, kSelectTimerId);
    tree_->Bind(wxEVT_TREE_SEL_CHANGED, &FileTreePanel::on_selection_changed,
                this);
    tree_->Bind(wxEVT_TREE_ITEM_EXPANDED, &FileTreePanel::on_expanded, this);
    tree_->Bind(wxEVT_TREE_ITEM_COLLAPSED, &FileTreePanel::on_collapsed, this);
    tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, &FileTreePanel::on_activated, this);
    tree_->Bind(wxEVT_LEFT_DOWN, &FileTreePanel::on_left_click, this);
    tree_->Bind(wxEVT_TREE_ITEM_MENU, &FileTreePanel::on_context_menu, this);
    // BEGIN_DRAG must be bound for a drag to happen at all: wxTreeCtrl only
    // starts one if a handler calls Allow(), so leaving this unbound is how
    // the tree behaved before -- inert, not broken.
    tree_->Bind(wxEVT_TREE_BEGIN_DRAG, &FileTreePanel::on_begin_drag, this);
    tree_->Bind(wxEVT_TREE_END_DRAG, &FileTreePanel::on_end_drag, this);
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
    entries_.clear();
    rebuild();       // show the root rows straight away, contents to follow
    start_scan();
}

void FileTreePanel::start_scan()
{
    // Scanning must not run on the UI thread.  It did once, in set_roots(),
    // and three real root folders were enough to stall the message loop past
    // the point where WebView2's asynchronous controller creation gives up --
    // CreateCoreWebView2Controller returned E_ABORT and the preview silently
    // never appeared.  The root rows are cheap to show without contents; the
    // contents arrive when they arrive.
    //
    // This is the one part of the sibling app's design that must NOT be
    // copied: PDF_Sherpa builds its entry list on the UI thread, which it can
    // afford only because nothing in that window is waiting on the message
    // loop the way a WebView2 controller is.
    const std::vector<Root> roots = roots_;
    if (roots.empty()) {
        return;
    }
    if (!alive_) {
        alive_ = std::make_shared<std::atomic<bool>>(true);
    }
    const unsigned generation = ++scan_generation_;
    std::shared_ptr<std::atomic<bool>> alive = alive_;
    // Read here, on the UI thread, and passed by value: the worker must not
    // reach back into Config while the user may be editing the same list.
    const std::vector<std::string> excluded =
        excluded_list_ ? excluded_list_() : std::vector<std::string>();

    std::thread([this, roots, generation, alive, excluded] {
        std::map<std::string, int> counts;
        std::vector<DocEntry> entries;
        std::set<std::string> truncated;
        std::map<std::string, std::string> excluded_seen;
        // Nothing may escape a detached thread: an uncaught exception here is
        // std::terminate, which is a hard crash with no message.
        //
        // The guard is per root, not around the loop. Wrapping the whole loop
        // meant one unreadable root discarded every other root's results too --
        // a network folder that was briefly unavailable made every tree in the
        // window read (0).
        for (std::size_t i = 0; i < roots.size(); ++i) {
            try {
                RootScan scan = scan_root(roots[i].path, excluded);
                counts.insert(scan.counts.begin(), scan.counts.end());
                if (scan.truncated) {
                    truncated.insert(norm_path(roots[i].path));
                }
                for (const std::string& folder : scan.excluded_folders) {
                    excluded_seen.emplace(norm_path(folder), folder);
                }
                for (DocEntry& entry : scan.entries) {
                    // scan_root knows nothing about the roots list, so which
                    // root an entry belongs to is stamped on here.
                    entry.root_index = i;
                    entries.push_back(std::move(entry));
                }
            } catch (...) {
                // This root contributes nothing; the others still appear.
            }
        }
        wxTheApp->CallAfter([this, counts, entries, truncated, excluded_seen,
                             generation, alive] {
            if (!alive->load() || generation != scan_generation_) {
                return;   // panel gone, or a newer scan already superseded us
            }
            counts_ = counts;
            entries_ = entries;
            truncated_roots_ = truncated;
            excluded_seen_ = excluded_seen;
            // rebuild() re-applies the remembered expansion itself, including
            // a set restored from the last session that had no rows to act on
            // until now.
            rebuild();
        });
    }).detach();
}

wxTreeItemId FileTreePanel::folder_item(std::size_t root_index,
                                        const std::string& relative_dir,
                                        std::map<std::string, wxTreeItemId>& made)
{
    // The tree item for a root-relative folder, creating every missing
    // ancestor on the way so the result is genuinely nested.
    //
    // Creating one node per distinct relative path instead would put
    // "notes/spec/annexes" on a single row hanging off the root -- a flat list
    // of slash-separated names wearing a tree's clothes.  Each component gets
    // its own node, and every later entry beneath it reuses that node.
    if (root_index >= root_items_.size()) {
        return wxTreeItemId();
    }
    const wxTreeItemId root_item = root_items_[root_index];
    const std::string root_path = roots_[root_index].path;
    if (relative_dir.empty()) {
        return root_item;
    }
    // A flattened root swallows everything beneath it: every document hangs
    // directly off it and no subfolder node is made at all.
    if (is_flat_folder_ && is_flat_folder_(root_path)) {
        return root_item;
    }

    // Cache keys are qualified with the root index, and must be.  Roots may
    // nest -- a workspace folder added as a root, and one repo inside it added
    // as a second -- and then the SAME absolute folder is reachable from two
    // roots.  Keyed on the path alone, the second root's walk found the first
    // root's node and hung its documents there: the file appeared twice under
    // one root and not at all under the other, with the folder count beside it
    // still reading 1.  It only showed up once the scan reached deep enough to
    // produce the collision.
    const std::string key_prefix = std::to_string(root_index) + "|";

    wxTreeItemId parent = root_item;
    std::string here = root_path;
    std::size_t start = 0;
    int depth = 0;
    // Bounded by the path's own length, and by the same depth ceiling the
    // filtered walk used (Rule of 10).
    while (start <= relative_dir.size() && depth <= kMaxFilterDepth) {
        const std::size_t slash = relative_dir.find('/', start);
        const std::string component =
            relative_dir.substr(start, (slash == std::string::npos)
                                           ? std::string::npos
                                           : slash - start);
        if (!component.empty()) {
            ++depth;
            here = path_to_utf8(path_from_utf8(here) /
                                path_from_utf8(component));
            const std::string prefix = key_prefix + norm_path(here);
            const auto found = made.find(prefix);
            if (found != made.end()) {
                parent = found->second;
            } else {
                parent = tree_->AppendItem(parent, folder_label(here, component));
                tree_->SetItemData(parent, new ItemData(here, false));
                made.emplace(prefix, parent);
            }
            // Stop at the nearest flattened ancestor: everything below it is
            // listed against it rather than getting nodes of its own.
            if (is_flat_folder_ && is_flat_folder_(here)) {
                return parent;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return parent;
}

wxString FileTreePanel::folder_label(const std::string& path,
                                     const std::string& name) const
{
    const std::string norm = norm_path(path);
    const auto found = counts_.find(norm);
    const int count = (found == counts_.end()) ? 0 : found->second;
    wxString label =
        wxString::FromUTF8(name) + wxString::Format("  (%d)", count);
    // An excluded folder has no count because nothing under it was walked, so
    // without this it reads as an empty folder rather than a skipped one.
    if (excluded_seen_.count(norm) != 0) {
        label += L"  (excluded)";
    }
    // The walk stopped before the end of this root, so the count below it is a
    // floor, not a total.  Saying nothing is what made a half-scanned
    // workspace indistinguishable from a fully scanned one.
    if (truncated_roots_.count(norm) != 0) {
        label += L"  (partial — scan limit reached)";
    }
    // Say so on the row.  A flattened folder shows no subfolders at all, so
    // without the marker there is nothing to distinguish it from one that
    // genuinely has none -- and the only way to check was to raise the context
    // menu and look at the tick.
    if (is_flat_folder_ && is_flat_folder_(path)) {
        label += kFlatSuffix;
    }
    return label;
}

void FileTreePanel::rebuild()
{
    // Set BEFORE DeleteAllItems, not just around the expanding below:
    // destroying an expanded row raises a collapse event, and treating those
    // as the user closing folders emptied the remembered set a rebuild at a
    // time -- the tree came back fully collapsed after a filter was typed and
    // cleared.
    applying_expansion_ = true;
    // Freeze: this builds every row in one go, and without it a few thousand
    // documents repaint the control once per item.
    tree_->Freeze();
    tree_->DeleteAllItems();
    root_items_.clear();
    const wxTreeItemId hidden = tree_->AddRoot("roots");
    const std::string needle = lowered(std::string(filter_->GetValue().ToUTF8()));
    const bool searching = !content_query().empty();

    // One node per configured root, made up front so the roots appear in their
    // configured order even when a later one has nothing to show.
    for (const Root& root : roots_) {
        const wxTreeItemId item =
            tree_->AppendItem(hidden, folder_label(root.path, root.name));
        tree_->SetItemData(item, new ItemData(root.path, false));
        tree_->SetItemBold(item, true);
        root_items_.push_back(item);
    }

    // Which entries survive the name filter.  A folder with nothing left under
    // it never gets a node, so filtering prunes the tree rather than replacing
    // it with a flat list -- the structure stays visible while you type.
    std::vector<const DocEntry*> shown;
    shown.reserve(entries_.size());
    for (const DocEntry& entry : entries_) {   // bounded by the scan's own cap
        if (needle.empty() ||
            lowered(entry.name).find(needle) != std::string::npos) {
            shown.push_back(&entry);
        }
    }

    // Two passes, so a folder's subfolders come before its files -- the order
    // the per-directory listing used to produce.  Pass one makes every folder
    // node that survives; pass two hangs the documents off them.
    std::map<std::string, wxTreeItemId> made;
    for (const DocEntry* entry : shown) {
        folder_item(entry->root_index, entry->relative_dir, made);
    }

    // Rows for the folders the scan skipped.  No document put them there, so
    // without this pass an excluded folder simply vanishes -- and with nothing
    // to right-click, the exclusion could be made but never undone.  Only when
    // nothing is being filtered or searched: a query is a question about
    // documents, and a folder holding none of them is not an answer.
    if (needle.empty() && !searching) {
        for (const auto& pair : excluded_seen_) {   // bounded by the exclusions
            const std::string& absolute = pair.second;
            for (std::size_t i = 0; i < roots_.size(); ++i) {
                const std::string relative =
                    relative_under(roots_[i].path, absolute);
                if (relative.empty()) {
                    continue;   // not under this root
                }
                folder_item(i, relative, made);
                break;
            }
        }
    }
    for (const DocEntry* entry : shown) {
        const wxTreeItemId parent =
            folder_item(entry->root_index, entry->relative_dir, made);
        if (!parent.IsOk()) {
            continue;
        }
        const wxTreeItemId file =
            tree_->AppendItem(parent, wxString::FromUTF8(entry->name));
        tree_->SetItemData(file, new ItemData(entry->path, true));
    }

    // Expanding done here is the rebuild's doing, not the user's, and must not
    // be recorded as a choice either -- applying_expansion_ has been set since
    // the top of this function and stays set until the tree is finished.
    for (std::size_t i = 0; i < root_items_.size(); ++i) {
        if (searching) {
            append_content_hits(root_items_[i], roots_[i].path);
        }
        if (!needle.empty() || searching) {
            // While filtering or searching, open everything: a match three
            // folders down is no use if finding it still means hunting for it.
            tree_->Expand(root_items_[i]);
            expand_all_under(root_items_[i], 0);
        } else {
            // Otherwise put back exactly what the user had open.  Doing this
            // on every rebuild is what lets a filter be typed and cleared
            // without losing your place.
            apply_expansion(root_items_[i], 0);
        }
    }
    applying_expansion_ = false;
    tree_->Thaw();
}

void FileTreePanel::apply_expansion(const wxTreeItemId& item, int depth)
{
    if (depth > kMaxFilterDepth || !item.IsOk()) {
        return;
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(item));
    if (data == nullptr || data->is_file()) {
        return;
    }
    if (user_expanded_.count(norm_path(data->path())) != 0) {
        tree_->Expand(item);
    }
    // Descend regardless of whether this folder was wanted: a saved deep
    // folder still reopens when an ancestor above it was left closed.
    wxTreeItemIdValue cookie;
    for (wxTreeItemId child = tree_->GetFirstChild(item, cookie);
         child.IsOk(); child = tree_->GetNextChild(item, cookie)) {
        apply_expansion(child, depth + 1);
    }
}

void FileTreePanel::on_expanded(wxTreeEvent& event)
{
    event.Skip();
    if (applying_expansion_) {
        return;   // the rebuild's own doing, not a choice to remember
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (data != nullptr && !data->is_file()) {
        user_expanded_.insert(norm_path(data->path()));
    }
}

void FileTreePanel::on_collapsed(wxTreeEvent& event)
{
    event.Skip();
    if (applying_expansion_) {
        return;
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (data != nullptr && !data->is_file()) {
        user_expanded_.erase(norm_path(data->path()));
    }
}

void FileTreePanel::expand_all_under(const wxTreeItemId& item, int depth)
{
    if (depth > kMaxFilterDepth || !item.IsOk()) {
        return;
    }
    wxTreeItemIdValue cookie;
    for (wxTreeItemId child = tree_->GetFirstChild(item, cookie);
         child.IsOk(); child = tree_->GetNextChild(item, cookie)) {
        auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(child));
        if (data != nullptr && !data->is_file()) {
            tree_->Expand(child);
            expand_all_under(child, depth + 1);
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

void FileTreePanel::on_selection_changed(wxTreeEvent& event)
{
    event.Skip();
    // A rebuild selects and deselects rows as it destroys and recreates them.
    // Acting on those would open a document every time the filter changed --
    // the same trap as the collapse events that used to empty the remembered
    // expansion set, and the same guard answers it.
    if (applying_expansion_ || !on_preview_) {
        return;
    }
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (data == nullptr || !data->is_file()) {
        select_timer_.Stop();   // a folder shows nothing; stop a pending open
        return;
    }
    pending_preview_ = data->path();
    // Whether the keyboard was here when the row changed.  Showing a document
    // takes focus away -- WebView2 claims it on navigation -- and without
    // giving it back the very next arrow key goes somewhere else, so browsing
    // moved exactly one row and then appeared to stop working.
    preview_from_tree_ = wxWindow::FindFocus() == tree_;
    // Restarted on every change, so holding an arrow key down travels the list
    // and opens only where it stops.
    select_timer_.Start(kSelectDebounceMs, wxTIMER_ONE_SHOT);
}

void FileTreePanel::on_select_timer(wxTimerEvent&)
{
    if (pending_preview_.empty() || !on_preview_) {
        return;
    }
    // Copied out before the call: showing the document can rebuild the tree,
    // which raises selection events and would reassign the member underneath
    // us.
    const std::string path = pending_preview_;
    pending_preview_.clear();
    on_preview_(path);
    // Only when the keyboard was in the tree to begin with: a browse triggered
    // by a click while the caret was in the editor must not yank focus back.
    if (preview_from_tree_ && tree_ != nullptr) {
        tree_->SetFocus();
    }
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
    // Show the change at once from the entries we have, then re-scan in the
    // background to correct them.  Doing it the other way round would leave
    // a renamed file looking untouched until the scan finished.  There is no
    // preserve-expansion dance any more: rebuild() re-applies the remembered
    // set itself, every time.
    rebuild();
    start_scan();
}

void FileTreePanel::scroll_fully_left()
{
    // EnsureVisible scrolls sideways to bring a whole label into view, which
    // on a deep folder with long filenames pushes the start of every *other*
    // row off the left edge.  The start of a name matters more than its tail,
    // so scroll back to the left margin.  wx has no API for this; the native
    // tree control does.  (Learned in PDF_Sherpa's PdfListPane, which hosts
    // the same control and hit the same thing.)
    const auto handle = tree_->GetHandle();
    if (handle != nullptr) {
        ::SendMessageW(static_cast<HWND>(handle), WM_HSCROLL, SB_LEFT, 0);
    }
}

std::vector<std::string> FileTreePanel::expanded_folders() const
{
    // What the user chose, not what is on screen: with a filter active the
    // tree is fully opened down to the matches, and saving that would reopen
    // the whole tree next launch.
    return std::vector<std::string>(user_expanded_.begin(),
                                    user_expanded_.end());
}

void FileTreePanel::set_expanded_folders(
    const std::vector<std::string>& folders)
{
    user_expanded_.clear();
    for (const std::string& path : folders) {   // bounded by the saved list
        if (!path.empty()) {
            user_expanded_.insert(norm_path(path));
        }
    }
    // Apply to what exists now.  At startup that is nothing -- the scan has
    // not landed -- but every rebuild applies the set again, so it takes
    // effect as soon as there are rows.
    applying_expansion_ = true;
    const wxTreeItemId hidden = tree_->GetRootItem();
    if (hidden.IsOk()) {
        wxTreeItemIdValue cookie;
        for (wxTreeItemId root = tree_->GetFirstChild(hidden, cookie);
             root.IsOk(); root = tree_->GetNextChild(hidden, cookie)) {
            apply_expansion(root, 0);
        }
    }
    applying_expansion_ = false;
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
    // A configured root, as opposed to a folder inside one.  Compared by path
    // rather than by depth, because a root row is the only row whose path is
    // one of the configured roots.
    bool is_root_row = false;
    for (const Root& root : roots_) {   // bounded by the roots list
        if (norm_path(root.path) == norm_path(path)) {
            is_root_row = true;
            break;
        }
    }

    wxMenu menu;
    if (is_file) {
        menu.Append(kIdOpen, "&Open");
        // Offered on any document, and disabled on one that is already a tech
        // note -- greyed rather than hidden, so it is discoverable on the
        // documents where it does nothing as well as the ones where it does.
        if (on_promote_) {
            wxMenuItem* promote =
                menu.Append(kIdPromoteTechNote, L"Update as &TechNote");
            promote->Enable(!is_tech_note_ || !is_tech_note_(path));
        }
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
    // Offered on a subfolder, never on a root: excluding a root would leave it
    // in the folders list contributing nothing, which is what removing it is
    // for.  The point of this is the generated folder inside a root -- a build
    // tree or a cache -- that costs the scan far more than it is worth.
    if (!is_file && on_toggle_excluded_ && !is_root_row) {
        const bool excluded = is_excluded_folder_ && is_excluded_folder_(path);
        menu.AppendCheckItem(kIdExclude, "S&kip when scanning")
            ->Check(excluded);
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
    // Reported, not done here: the frame owns the open document, so it is the
    // only thing that can tell whether this file is being edited right now.
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_promote_) {
            on_promote_(path);
        }
    }, kIdPromoteTechNote);
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
        // Redraw in the new shape.  rebuild() puts the remembered expansion
        // back by itself, so the folder just toggled stays where it was
        // instead of the tree snapping shut to its roots.
        rebuild();
        const wxTreeItemId item = find_item(path);
        if (item.IsOk()) {
            // Expand as well as select: the whole point of the command is to
            // see the flat listing, and the folder may have been collapsed
            // when the menu was raised.
            tree_->Expand(item);
            tree_->SelectItem(item);
            tree_->EnsureVisible(item);
            scroll_fully_left();
        }
    }, kIdFlatList);
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_toggle_excluded_) {
            on_toggle_excluded_(path);   // flips and persists
        }
        // A full rescan, not a rebuild: excluding a folder changes what the
        // walk collects, and un-excluding one needs the documents underneath
        // it read for the first time.  Nothing in memory can answer either.
        refresh();
    }, kIdExclude);
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

std::vector<std::string> FileTreePanel::document_paths() const
{
    std::vector<std::string> paths;
    paths.reserve(entries_.size());
    for (const DocEntry& entry : entries_) {   // bounded by the scan's own cap
        paths.push_back(entry.path);
    }
    return paths;
}

bool FileTreePanel::knows_document(const std::string& path) const
{
    const std::string target = norm_path(path);
    if (target.empty()) {
        return false;
    }
    for (const DocEntry& entry : entries_) {   // bounded by the scan's own cap
        if (norm_path(entry.path) == target) {
            return true;
        }
    }
    return false;
}

std::string FileTreePanel::owning_root(const std::string& path) const
{
    const std::string target = norm_path(path);
    for (const Root& root : roots_) {   // bounded by the roots list
        if (root.path.empty()) {
            continue;
        }
        std::string base = norm_path(root.path);
        if (!base.empty() && (base.back() == '\\' || base.back() == '/')) {
            base.pop_back();
        }
        if (target == base) {
            return root.path;
        }
        if (target.size() > base.size() &&
            target.compare(0, base.size(), base) == 0 &&
            (target[base.size()] == '\\' || target[base.size()] == '/')) {
            return root.path;
        }
    }
    return {};
}

void FileTreePanel::on_begin_drag(wxTreeEvent& event)
{
    drag_source_.clear();
    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    // Files only.  A folder drag is refused by simply not allowing the event,
    // so the cursor never suggests a drop that would not happen.
    if (data == nullptr || !data->is_file()) {
        return;
    }
    drag_source_ = data->path();
    event.Allow();
}

void FileTreePanel::on_end_drag(wxTreeEvent& event)
{
    const std::string source = drag_source_;
    drag_source_.clear();
    if (source.empty()) {
        return;
    }

    auto* data = dynamic_cast<ItemData*>(tree_->GetItemData(event.GetItem()));
    if (data == nullptr) {
        return;   // dropped on empty space, or on the hidden root
    }
    // Dropping onto a file means "put it beside this one", which is the
    // folder it lives in -- landing on a file is far easier to aim at than
    // the folder row above it.
    const std::string target_dir =
        data->is_file()
            ? path_to_utf8(path_from_utf8(data->path()).parent_path())
            : data->path();
    if (target_dir.empty()) {
        return;
    }
    move_document(source, target_dir);
}

void FileTreePanel::move_document(const std::string& source,
                                  const std::string& target_dir)
{
    const std::filesystem::path from = path_from_utf8(source);
    const std::filesystem::path to =
        path_from_utf8(target_dir) / from.filename();

    if (norm_path(path_to_utf8(from.parent_path())) == norm_path(target_dir)) {
        return;   // dropped back where it already is
    }

    std::error_code ec;
    const bool collides = std::filesystem::exists(to, ec);
    ec.clear();
    const std::string from_root = owning_root(source);
    const std::string to_root = owning_root(target_dir);
    // Both empty (neither under a configured root) counts as not crossing --
    // there is no boundary to cross.
    const bool crosses_root = norm_path(from_root) != norm_path(to_root);

    const wxString name = wxString::FromUTF8(path_to_utf8(from.filename()));
    if (collides) {
        // Replacing a document is the one outcome nothing can undo, so it is
        // always asked, and the question says "replace" rather than "move".
        const int answer = wxMessageBox(
            "A file with that name is already there.\n\nReplace it?\n\n" +
                name + L"\n→ " + wxString::FromUTF8(target_dir),
            "MD Boss", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
        if (answer != wxYES) {
            return;
        }
    } else if (crosses_root) {
        // Between two top-level folders is a bigger move than it looks -- the
        // document leaves the tree it was filed under -- so it is confirmed
        // even though nothing is destroyed.
        const int answer = wxMessageBox(
            "Move to a different top-level folder?\n\n" + name + L"\n→ " +
                wxString::FromUTF8(target_dir),
            "MD Boss", wxYES_NO | wxICON_QUESTION, this);
        if (answer != wxYES) {
            return;
        }
    }

    std::filesystem::rename(from, to, ec);
    if (ec) {
        // rename() cannot cross volumes.  Copy then remove, and only remove
        // once the copy is known to be there -- the reverse order turns a
        // failed move into a lost document.
        std::error_code copy_ec;
        std::filesystem::copy_file(
            from, to, std::filesystem::copy_options::overwrite_existing,
            copy_ec);
        if (copy_ec || !std::filesystem::exists(to, copy_ec)) {
            wxMessageBox("Could not move:\n" + name + "\n\n" +
                             wxString::FromUTF8(ec.message()),
                         "MD Boss", wxOK | wxICON_ERROR, this);
            return;
        }
        std::filesystem::remove(from, copy_ec);
        if (copy_ec) {
            wxMessageBox("Copied, but the original could not be removed:\n" +
                             name,
                         "MD Boss", wxOK | wxICON_WARNING, this);
        }
    }

    // Told before the rescan, so the app can rewrite the favourite, the recent
    // and the open document's path while the old one is still known.
    if (on_path_moved_) {
        on_path_moved_(source, path_to_utf8(to));
    }
    refresh();
    const wxTreeItemId moved = find_item(path_to_utf8(to));
    if (moved.IsOk()) {
        tree_->SelectItem(moved);
        tree_->EnsureVisible(moved);
        scroll_fully_left();
    }
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
    // Renaming moves the file too, so it owes the same notification a drag
    // does.  It did not send one before the drag feature existed, which meant
    // renaming the OPEN document left the frame holding the old path and
    // Ctrl+S writing the file back under its previous name.
    if (on_path_moved_) {
        on_path_moved_(path, path_to_utf8(target));
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
