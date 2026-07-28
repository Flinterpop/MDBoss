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

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Config.h"

namespace mdboss {

class FileTreePanel : public wxPanel {
public:
    explicit FileTreePanel(wxWindow* parent);
    ~FileTreePanel() override;

    // Rebuild from `roots`, recomputing the per-folder Markdown counts.
    void set_roots(const std::vector<Root>& roots);

    // Called when the user activates a Markdown file in the tree.
    void set_on_open(std::function<void(const std::string&)> handler)
    {
        on_open_ = std::move(handler);
    }

    // Toggling a file's favourite status, and asking whether it is one, are
    // the app's business -- the tree only shows the menu item.
    void set_favorite_hooks(std::function<void(const std::string&)> toggle,
                            std::function<bool(const std::string&)> query)
    {
        on_toggle_favorite_ = std::move(toggle);
        is_favorite_ = std::move(query);
    }

    // Rebuild now with the counts in hand, and re-scan in the background.
    // Keeps whatever the user had expanded.
    void refresh();

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
    void on_context_menu(wxTreeEvent& event);
    void rebuild_preserving_expansion();
    void collect_expanded(const wxTreeItemId& item,
                          std::vector<std::string>& out, int depth) const;
    void restore_expanded(const wxTreeItemId& item,
                          const std::vector<std::string>& paths, int depth);

    // Context-menu actions.  Each ends in refresh() so counts stay honest.
    void new_document(const std::string& dir);
    void new_folder(const std::string& dir);
    void rename_path(const std::string& path);
    void delete_path(const std::string& path);

    // Counting runs on a worker thread; see the .cpp for why that is not
    // optional.  `scan_` is generation-stamped so a stale result from a
    // superseded scan is discarded, and `alive_` lets the completion lambda
    // tell that this panel is gone.
    void start_scan();

    wxTextCtrl* filter_ = nullptr;
    wxTreeCtrl* tree_ = nullptr;
    std::vector<Root> roots_;
    std::map<std::string, int> counts_;
    std::shared_ptr<std::atomic<bool>> alive_;
    unsigned scan_generation_ = 0;
    std::function<void(const std::string&)> on_open_;
    std::function<void(const std::string&)> on_toggle_favorite_;
    std::function<bool(const std::string&)> is_favorite_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_TREE_PANEL_H
