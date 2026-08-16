// The files pane: a filter box over a tree of the configured root folders.
//
// Folders show a recursive Markdown count, folders with none anywhere beneath
// them are omitted, children are loaded lazily on expand, and a single click
// on a file opens it (Enter and double-click open it too).

#ifndef MDBOSS_APP_FILE_TREE_PANEL_H
#define MDBOSS_APP_FILE_TREE_PANEL_H

#include <wx/checkbox.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/treectrl.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Config.h"
#include "FileScan.h"

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

    // Whether a folder -- any folder, root or subfolder -- is shown as a flat
    // list, and toggling it.  The tree only shows the menu item and reflects
    // the state; the app owns where it is stored.  `toggle` should flip and
    // persist; the tree rebuilds after.
    void set_flat_hooks(std::function<bool(const std::string&)> query,
                        std::function<void(const std::string&)> toggle)
    {
        is_flat_folder_ = std::move(query);
        on_toggle_flat_ = std::move(toggle);
    }

    // "Import files into MD_Inbox…", which the tree offers wherever the menu
    // is raised -- including empty space, as the Python app does, because the
    // command is about the roots as a whole and not the row under the cursor.
    void set_on_import_to_inbox(std::function<void()> handler)
    {
        on_import_to_inbox_ = std::move(handler);
    }

    // "Manage templates…" and "Manage folders…", both of which the tree
    // offers but neither of which it owns -- one opens a folder, the other a
    // dialog the frame holds.
    void set_manage_hooks(std::function<void()> templates,
                          std::function<void()> folders)
    {
        on_manage_templates_ = std::move(templates);
        on_manage_folders_ = std::move(folders);
    }

    // Rebuild now with the counts in hand, and re-scan in the background.
    // Keeps whatever the user had expanded.
    void refresh();

    // The folders open right now, as normalised paths, and re-applying a set
    // saved earlier.  Together these let the frame persist the tree's shape
    // across a restart instead of reopening collapsed every launch.
    //
    // Applying is safe before the first scan finishes: whatever is on screen
    // when the counts arrive is what rebuild_preserving_expansion() carries
    // over, so an early restore survives the rebuild rather than racing it.
    std::vector<std::string> expanded_folders() const;
    void set_expanded_folders(const std::vector<std::string>& folders);

private:
    // True if `path` is one of the configured top-level roots.
    bool is_root_path(const std::string& path) const;
    // Add the files under `root` that matched on their text, each with the
    // matching line beneath it.  Skips files the name filter already listed.
    void append_content_hits(const wxTreeItemId& item, const std::string& root);
    void populate(const wxTreeItemId& item, const std::string& path);
    void on_expanding(wxTreeEvent& event);
    void on_activated(wxTreeEvent& event);
    void on_left_click(wxMouseEvent& event);
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
    // The item currently showing `path`, or an invalid id if it is not on
    // screen.  Rebuilding destroys every item, so putting the user back where
    // they were means finding the row again by path afterwards.
    // Undo the sideways scroll EnsureVisible does; see the .cpp.
    void scroll_fully_left();
    wxTreeItemId find_item(const std::string& path) const;
    wxTreeItemId find_item_under(const wxTreeItemId& item,
                                 const std::string& key, int depth) const;

    // Context-menu actions.  Each ends in refresh() so counts stay honest.
    // An empty `template_path` gives the blank "# <name>" body; otherwise the
    // template is read and its placeholders substituted.
    void new_document(const std::string& dir, const std::string& template_path);
    void new_folder(const std::string& dir);
    void rename_path(const std::string& path);
    void delete_path(const std::string& path);

    // Counting runs on a worker thread; see the .cpp for why that is not
    // optional.  `scan_` is generation-stamped so a stale result from a
    // superseded scan is discarded, and `alive_` lets the completion lambda
    // tell that this panel is gone.
    void start_scan();

    // Content search.  Reading every file cannot happen on the UI thread and
    // cannot happen per keystroke, so the box starts a timer, the timer starts
    // a worker, and the worker's results are dropped if the query moved on --
    // the same generation-stamped shape as start_scan().
    void on_search_timer(wxTimerEvent& event);
    void start_content_search();
    // The current query, or empty when content search is off, the box is
    // empty, or the text is too short to be worth searching for.
    std::string content_query() const;

    wxTextCtrl* filter_ = nullptr;
    wxCheckBox* contents_ = nullptr;
    wxTreeCtrl* tree_ = nullptr;
    std::vector<Root> roots_;
    std::map<std::string, int> counts_;
    // Results of the last completed search, keyed by root path.
    std::map<std::string, std::vector<ContentMatch>> content_hits_;
    // The query those results answer, so a stale tree can say so.
    std::string searched_for_;
    bool searching_ = false;
    wxTimer search_timer_;
    std::shared_ptr<std::atomic<bool>> alive_;
    unsigned scan_generation_ = 0;
    // Atomic, unlike scan_generation_: the search worker polls this to notice
    // it has been superseded mid-walk, so it is read off the UI thread.
    std::atomic<unsigned> search_generation_{0};
    std::function<void(const std::string&)> on_open_;
    std::function<void(const std::string&)> on_toggle_favorite_;
    std::function<bool(const std::string&)> is_favorite_;
    std::function<bool(const std::string&)> is_flat_folder_;
    std::function<void(const std::string&)> on_toggle_flat_;
    std::function<void()> on_import_to_inbox_;
    std::function<void()> on_manage_templates_;
    std::function<void()> on_manage_folders_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_TREE_PANEL_H
