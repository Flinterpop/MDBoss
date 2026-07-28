#include "FileTreePanel.h"

#include <wx/sizer.h>

#include <cassert>

#include "FileScan.h"

namespace mdboss {
namespace {

// Bound on how deep a filtered search will walk (Rule of 10).
constexpr int kMaxFilterDepth = 24;

// Non-ASCII UI text must be a WIDE literal.  A narrow "…" in a UTF-8 source
// is handed to wxString as bytes and decoded in the current ANSI codepage,
// which renders as "â€¦".  Wide literals are unambiguous.
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
    tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT |
                               wxTR_LINES_AT_ROOT);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(filter_, 0, wxEXPAND | wxALL, 2);
    sizer->Add(tree_, 1, wxEXPAND);
    SetSizer(sizer);

    filter_->Bind(wxEVT_TEXT, &FileTreePanel::on_filter, this);
    tree_->Bind(wxEVT_TREE_ITEM_EXPANDING, &FileTreePanel::on_expanding, this);
    tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, &FileTreePanel::on_activated, this);
}

void FileTreePanel::set_roots(const std::vector<Root>& roots)
{
    roots_ = roots;
    counts_.clear();
    for (const Root& root : roots_) {
        const std::map<std::string, int> counts =
            md_counts_for_root(root.path);
        counts_.insert(counts.begin(), counts.end());
    }
    rebuild();
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

        if (needle.empty()) {
            // Lazily populated: a placeholder makes the expander appear.
            tree_->AppendItem(item, kLazyPlaceholder);
        } else {
            populate_filtered(item, root.path, needle, 0);
            tree_->Expand(item);
        }
    }
}

void FileTreePanel::populate(const wxTreeItemId& item, const std::string& path)
{
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

void FileTreePanel::on_filter(wxCommandEvent& event)
{
    rebuild();
    event.Skip();
}

}  // namespace mdboss
