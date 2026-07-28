// "Manage folders…": add, rename, reorder and remove the root folders the
// files pane shows.  Capped at the same five roots the Python app allows.

#ifndef MDBOSS_APP_FOLDERS_DIALOG_H
#define MDBOSS_APP_FOLDERS_DIALOG_H

#include <wx/dialog.h>
#include <wx/listbox.h>

#include <vector>

#include "Config.h"

namespace mdboss {

class FoldersDialog : public wxDialog {
public:
    FoldersDialog(wxWindow* parent, std::vector<Root> roots);

    const std::vector<Root>& roots() const { return roots_; }

private:
    void reload();
    void on_add(wxCommandEvent& event);
    void on_remove(wxCommandEvent& event);
    void on_rename(wxCommandEvent& event);
    void move_selected(int delta);

    wxListBox* list_ = nullptr;
    std::vector<Root> roots_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FOLDERS_DIALOG_H
