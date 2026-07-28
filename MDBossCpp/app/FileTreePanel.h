// The files pane: a filter box over a tree of the configured root folders.
//
// Behaviour mirrors the Python app: folders show a recursive Markdown count,
// folders with none anywhere beneath them are omitted, children are loaded
// lazily on expand, and activating a file opens it.

#ifndef MDBOSS_APP_FILE_TREE_PANEL_H
#define MDBOSS_APP_FILE_TREE_PANEL_H

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Config.h"

namespace mdboss {

class FileTreePanel : public wxPanel {
public:
    explicit FileTreePanel(wxWindow* parent);

    // Rebuild from `roots`, recomputing the per-folder Markdown counts.
    void set_roots(const std::vector<Root>& roots);

    // Called when the user activates a Markdown file in the tree.
    void set_on_open(std::function<void(const std::string&)> handler)
    {
        on_open_ = std::move(handler);
    }

private:
    void populate(const wxTreeItemId& item, const std::string& path);
    void on_expanding(wxTreeEvent& event);
    void on_activated(wxTreeEvent& event);
    void on_filter(wxCommandEvent& event);
    void rebuild();
    // Filtered view: a flat list of matching files per root, because a
    // wxTreeCtrl item cannot be hidden the way a QTreeWidgetItem can.
    void populate_filtered(const wxTreeItemId& root_item,
                           const std::string& path, const std::string& needle,
                           int depth);

    wxTextCtrl* filter_ = nullptr;
    wxTreeCtrl* tree_ = nullptr;
    std::vector<Root> roots_;
    std::map<std::string, int> counts_;
    std::function<void(const std::string&)> on_open_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_TREE_PANEL_H
