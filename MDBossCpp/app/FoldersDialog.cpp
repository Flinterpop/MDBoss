#include "FoldersDialog.h"

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include <cassert>

namespace mdboss {
namespace {

// Matches the Python app's cap.
constexpr std::size_t kMaxRoots = 5;

constexpr int kIdAdd = wxID_HIGHEST + 20;
constexpr int kIdRemove = wxID_HIGHEST + 21;
constexpr int kIdRename = wxID_HIGHEST + 22;
constexpr int kIdUp = wxID_HIGHEST + 23;
constexpr int kIdDown = wxID_HIGHEST + 24;

}  // namespace

FoldersDialog::FoldersDialog(wxWindow* parent, std::vector<Root> roots)
    : wxDialog(parent, wxID_ANY, "Manage folders", wxDefaultPosition,
               wxSize(560, 320), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      roots_(std::move(roots))
{
    list_ = new wxListBox(this, wxID_ANY);

    auto* buttons = new wxBoxSizer(wxVERTICAL);
    buttons->Add(new wxButton(this, kIdAdd, L"&Add…"), 0, wxEXPAND | wxBOTTOM, 4);
    buttons->Add(new wxButton(this, kIdRename, L"Re&name…"), 0,
                 wxEXPAND | wxBOTTOM, 4);
    buttons->Add(new wxButton(this, kIdRemove, "&Remove"), 0,
                 wxEXPAND | wxBOTTOM, 12);
    buttons->Add(new wxButton(this, kIdUp, "Move &up"), 0, wxEXPAND | wxBOTTOM,
                 4);
    buttons->Add(new wxButton(this, kIdDown, "Move &down"), 0, wxEXPAND);

    auto* top = new wxBoxSizer(wxHORIZONTAL);
    top->Add(list_, 1, wxEXPAND | wxALL, 8);
    top->Add(buttons, 0, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 8);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(top, 1, wxEXPAND);
    outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
               wxEXPAND | wxALL, 8);
    SetSizer(outer);

    Bind(wxEVT_BUTTON, &FoldersDialog::on_add, this, kIdAdd);
    Bind(wxEVT_BUTTON, &FoldersDialog::on_remove, this, kIdRemove);
    Bind(wxEVT_BUTTON, &FoldersDialog::on_rename, this, kIdRename);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(-1); }, kIdUp);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(1); }, kIdDown);

    reload();
}

void FoldersDialog::reload()
{
    const int selected = list_->GetSelection();
    wxArrayString labels;
    for (const Root& root : roots_) {
        labels.Add(wxString::FromUTF8(root.name) + L"   —   " +
                   wxString::FromUTF8(root.path));
    }
    list_->Set(labels);
    if (selected >= 0 && static_cast<std::size_t>(selected) < roots_.size()) {
        list_->SetSelection(selected);
    }
}

void FoldersDialog::on_add(wxCommandEvent&)
{
    if (roots_.size() >= kMaxRoots) {
        wxMessageBox(wxString::Format("At most %zu root folders.", kMaxRoots),
                     "MD Boss", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxDirDialog dialog(this, L"Choose a folder to add", "",
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const std::string path(dialog.GetPath().ToUTF8());
    for (const Root& existing : roots_) {
        if (existing.path == path) {
            return;   // already a root; adding it twice helps nobody
        }
    }
    Root root;
    root.path = path;
    root.name = std::string(wxFileName(dialog.GetPath()).GetFullName().ToUTF8());
    if (root.name.empty()) {
        root.name = path;
    }
    roots_.push_back(std::move(root));
    reload();
}

void FoldersDialog::on_remove(wxCommandEvent&)
{
    const int index = list_->GetSelection();
    if (index < 0 || static_cast<std::size_t>(index) >= roots_.size()) {
        return;
    }
    roots_.erase(roots_.begin() + index);
    reload();
}

void FoldersDialog::on_rename(wxCommandEvent&)
{
    const int index = list_->GetSelection();
    if (index < 0 || static_cast<std::size_t>(index) >= roots_.size()) {
        return;
    }
    Root& root = roots_[static_cast<std::size_t>(index)];
    const wxString name = wxGetTextFromUser(
        "Display name for this folder:", "MD Boss",
        wxString::FromUTF8(root.name), this);
    if (name.empty()) {
        return;
    }
    root.name = std::string(name.ToUTF8());
    reload();
}

void FoldersDialog::move_selected(int delta)
{
    const int index = list_->GetSelection();
    const int target = index + delta;
    if (index < 0 || target < 0 ||
        static_cast<std::size_t>(index) >= roots_.size() ||
        static_cast<std::size_t>(target) >= roots_.size()) {
        return;
    }
    std::swap(roots_[static_cast<std::size_t>(index)],
              roots_[static_cast<std::size_t>(target)]);
    reload();
    list_->SetSelection(target);
}

}  // namespace mdboss
