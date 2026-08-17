#include "PathListPanel.h"

#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <cassert>
#include <filesystem>

namespace mdboss {
namespace {

constexpr int kIdOpen = wxID_HIGHEST + 40;
constexpr int kIdExtra = wxID_HIGHEST + 41;
constexpr int kIdReveal = wxID_HIGHEST + 42;
constexpr int kIdCopyPath = wxID_HIGHEST + 43;
constexpr int kIdClear = wxID_HIGHEST + 44;
// Per-instance commands get ids from here up.  Bounded (Rule of 10) so the
// range cannot run into whatever the next constant above happens to be.
constexpr int kIdCommandBase = wxID_HIGHEST + 45;
constexpr std::size_t kMaxCommands = 4;

}  // namespace

void PathListPanel::add_menu_command(const wxString& label,
                                     std::function<void()> handler)
{
    assert(handler && "a menu command needs something to do");
    assert(commands_.size() < kMaxCommands && "too many list commands");
    if (!handler || commands_.size() >= kMaxCommands) {
        return;
    }
    commands_.emplace_back(label, std::move(handler));
}

PathListPanel::PathListPanel(wxWindow* parent, const wxString& title,
                             const wxString& extra_label)
    : wxPanel(parent, wxID_ANY), extra_label_(extra_label)
{
    auto* header = new wxStaticText(this, wxID_ANY, title);
    wxFont bold = header->GetFont();
    bold.MakeBold();
    header->SetFont(bold);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
    list_->AppendColumn("", wxLIST_FORMAT_LEFT, 400);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header, 0, wxLEFT | wxTOP | wxBOTTOM, 4);
    sizer->Add(list_, 1, wxEXPAND);
    SetSizer(sizer);

    // A single click opens, matching the files tree.  Double-click and Enter
    // still work, because ACTIVATED stays bound.
    //
    // Deliberately the mouse event and a hit-test, NOT wxEVT_LIST_ITEM_SELECTED:
    // selection also changes when arrowing through the list, so binding that
    // would open a document on every keypress -- and each one would raise the
    // unsaved-edits prompt for the document being left.  Keyboard navigation
    // does not come through here at all.
    list_->Bind(wxEVT_LEFT_DOWN, &PathListPanel::on_left_click, this);
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &PathListPanel::on_activated, this);
    list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &PathListPanel::on_context_menu,
                this);
}

void PathListPanel::set_paths(const std::vector<std::string>& paths)
{
    if (paths == paths_) {
        return;   // avoid clearing the user's selection on every render
    }
    paths_ = paths;

    list_->DeleteAllItems();
    for (std::size_t i = 0; i < paths_.size(); ++i) {
        const wxString full = wxString::FromUTF8(paths_[i]);
        const wxString name = wxFileName(full).GetFullName();
        const long row = list_->InsertItem(static_cast<long>(i),
                                           name.IsEmpty() ? full : name);
        std::error_code ec;
        const bool missing =
            !std::filesystem::exists(std::filesystem::path(paths_[i]), ec) ||
            ec;
        if (missing) {
            list_->SetItemTextColour(row, *wxRED);
        }
        list_->SetItem(row, 0, name.IsEmpty() ? full : name);
    }
    list_->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
}

std::string PathListPanel::selected_path() const
{
    const long row =
        list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (row < 0 || static_cast<std::size_t>(row) >= paths_.size()) {
        return {};
    }
    return paths_[static_cast<std::size_t>(row)];
}

void PathListPanel::on_left_click(wxMouseEvent& event)
{
    // Hit-test the click point rather than trusting the current selection:
    // the click may land on a row that is not selected yet, and that row is
    // the one to open.
    int flags = 0;
    const long row = list_->HitTest(event.GetPosition(), flags);
    if (row >= 0 && static_cast<std::size_t>(row) < paths_.size() &&
        on_activate_) {
        on_activate_(paths_[static_cast<std::size_t>(row)]);
    }
    event.Skip();   // let the list select and focus the row as usual
}

void PathListPanel::on_activated(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row >= 0 && static_cast<std::size_t>(row) < paths_.size() &&
        on_activate_) {
        on_activate_(paths_[static_cast<std::size_t>(row)]);
    }
}

void PathListPanel::on_context_menu(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= paths_.size()) {
        return;
    }
    const std::string path = paths_[static_cast<std::size_t>(row)];

    wxMenu menu;
    menu.Append(kIdOpen, "&Open");
    if (!extra_label_.IsEmpty()) {
        menu.Append(kIdExtra, extra_label_);
    }
    menu.AppendSeparator();
    menu.Append(kIdReveal, "Reveal in &Explorer");
    menu.Append(kIdCopyPath, "&Copy path");
    if (!commands_.empty()) {
        menu.AppendSeparator();
        for (std::size_t i = 0; i < commands_.size(); ++i) {
            menu.Append(kIdCommandBase + static_cast<int>(i),
                        commands_[i].first);
        }
    }
    if (on_clear_) {
        menu.AppendSeparator();
        menu.Append(kIdClear, "C&lear list");
    }
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        // Captured by index, not by reference: the vector outlives the menu
        // but a reference into it would not survive a later add.
        menu.Bind(wxEVT_MENU, [this, i](wxCommandEvent&) {
            assert(i < commands_.size() && "command vanished");
            commands_[i].second();
        }, kIdCommandBase + static_cast<int>(i));
    }

    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_activate_) {
            on_activate_(path);
        }
    }, kIdOpen);
    menu.Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
        if (on_extra_) {
            on_extra_(path);
        }
    }, kIdExtra);
    menu.Bind(wxEVT_MENU, [path](wxCommandEvent&) {
        // /select, needs the path quoted; an unquoted space silently opens
        // the wrong folder.
        const wxString command =
            "explorer.exe /select,\"" + wxString::FromUTF8(path) + "\"";
        wxExecute(command, wxEXEC_ASYNC);
    }, kIdReveal);
    menu.Bind(wxEVT_MENU, [path](wxCommandEvent&) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(
                new wxTextDataObject(wxString::FromUTF8(path)));
            wxTheClipboard->Close();
        }
    }, kIdCopyPath);
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (on_clear_) {
            on_clear_();
        }
    }, kIdClear);

    PopupMenu(&menu);
}

}  // namespace mdboss
