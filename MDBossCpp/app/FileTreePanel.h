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
#include <set>
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

    // Whether the scan skips a folder, and toggling it -- the same
    // query/toggle split as the flat hooks, and for the same reason: the tree
    // shows the state and the menu item, the app owns where it is kept.
    // `list` supplies the exclusions to the scan itself.
    void set_exclude_hooks(std::function<bool(const std::string&)> query,
                           std::function<void(const std::string&)> toggle,
                           std::function<std::vector<std::string>()> list)
    {
        is_excluded_folder_ = std::move(query);
        on_toggle_excluded_ = std::move(toggle);
        excluded_list_ = std::move(list);
    }

    // Called after a document has been moved on disk by a drag, with its old
    // path then its new one.
    //
    // The tree reports rather than reaches: favorites and recents are stored
    // as ABSOLUTE paths and the open document's path lives in the frame, so a
    // move that did not say so would silently orphan a favourite, break a
    // recent, and leave Ctrl+S writing the document back to where it used to
    // be.  The app owns all three; the tree only knows the move happened.
    void set_on_path_moved(
        std::function<void(const std::string&, const std::string&)> handler)
    {
        on_path_moved_ = std::move(handler);
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

    // Every document the last completed scan found, absolute paths.  The tree
    // already holds them, so anything wanting to read the documents in bulk
    // can use this instead of walking the disk a second time.
    std::vector<std::string> document_paths() const;

    // The folders the user has open, as normalised paths, and re-applying a
    // set saved earlier.  Together these let the frame persist the tree's
    // shape across a restart instead of reopening collapsed every launch.
    //
    // Handing over a set before the first scan finishes is fine: the tree
    // holds the set and every rebuild re-applies it, so it lands as soon as
    // there are rows to expand.
    std::vector<std::string> expanded_folders() const;
    void set_expanded_folders(const std::vector<std::string>& folders);

private:
    // Add the files under `root` that matched on their text, each with the
    // matching line beneath it.  Skips files the name filter already listed.
    void append_content_hits(const wxTreeItemId& item, const std::string& root);
    void on_activated(wxTreeEvent& event);
    void on_left_click(wxMouseEvent& event);
    void on_filter(wxCommandEvent& event);

    // Build every row from `entries_`.  Nothing here touches the disk: the
    // scan already did, and rebuilding from memory is what makes expanding a
    // folder instant and lets a filter prune the tree instead of flattening
    // it.
    void rebuild();
    // The item for a root-relative folder, creating any missing ancestor.
    // `made` caches what this rebuild has already created, keyed by normalised
    // path, so the second pass reuses the first pass's nodes.
    wxTreeItemId folder_item(std::size_t root_index,
                             const std::string& relative_dir,
                             std::map<std::string, wxTreeItemId>& made);
    // "<name>  (<count>)", plus the flat marker when the folder is flattened.
    wxString folder_label(const std::string& path,
                          const std::string& name) const;
    void expand_all_under(const wxTreeItemId& item, int depth);
    // Re-open the folders in `user_expanded_`.  Every rebuild ends here, which
    // is what makes the tree survive a filter being typed and cleared.
    void apply_expansion(const wxTreeItemId& item, int depth);
    // The user opening or closing a folder is the only thing that changes
    // `user_expanded_`; expanding done by a rebuild must not.
    void on_expanded(wxTreeEvent& event);
    void on_collapsed(wxTreeEvent& event);
    void on_context_menu(wxTreeEvent& event);
    // Dragging a document onto a folder moves it on disk.  Files only:
    // dragging a folder would relocate a whole subtree on one mis-drop, and
    // needs an ancestor check a file move does not.
    void on_begin_drag(wxTreeEvent& event);
    void on_end_drag(wxTreeEvent& event);
    // Perform the move, asking first only when it is risky -- crossing to a
    // different root folder, or landing on a name that already exists.  A
    // plain move within one root is silent, because confirming every drag on a
    // tree people also drag to scroll is its own kind of wrong.
    void move_document(const std::string& source, const std::string& target_dir);
    // The configured root `path` sits under, or empty when it is under none.
    std::string owning_root(const std::string& path) const;
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

    // Scanning runs on a worker thread; see the .cpp for why that is not
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
    // Every document under every root, and the recursive count for every
    // folder walked.  The tree is a view of these two; the disk is read only
    // by the scan that fills them.
    std::vector<DocEntry> entries_;
    std::map<std::string, int> counts_;
    // Roots whose walk stopped short of the whole tree, by normalised path, so
    // the row can say the list under it is incomplete.  A bound that can be
    // hit and never mentioned is how a workspace root silently lost 13 repos.
    std::set<std::string> truncated_roots_;
    // Folders the scan was told to skip: normalised path -> the path as it is
    // spelled on disk.  Both halves are needed -- the normalised form to mark
    // the row, the original to build it, since no document put it there and
    // the row is the only place the exclusion can be undone.
    std::map<std::string, std::string> excluded_seen_;
    // The row for each configured root, in configured order, valid between a
    // rebuild and the next one.
    std::vector<wxTreeItemId> root_items_;
    // Folders the USER has open, by normalised path.  Kept apart from what is
    // on screen because a filter opens everything down to its matches, and
    // that must not be mistaken for a choice: clearing the filter has to put
    // the tree back the way it was, not leave it fully expanded.
    std::set<std::string> user_expanded_;
    // Set while a rebuild expands rows itself, so its own expand events do not
    // land in user_expanded_.
    bool applying_expansion_ = false;
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
    std::function<bool(const std::string&)> is_excluded_folder_;
    std::function<void(const std::string&)> on_toggle_excluded_;
    std::function<std::vector<std::string>()> excluded_list_;
    std::function<void(const std::string&, const std::string&)> on_path_moved_;
    // The document a drag started on, held between BEGIN_DRAG and END_DRAG.
    // Empty when no drag is in progress, which is also how a drag that began
    // on something undraggable is distinguished from one that never began.
    std::string drag_source_;
    std::function<void()> on_import_to_inbox_;
    std::function<void()> on_manage_templates_;
    std::function<void()> on_manage_folders_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_TREE_PANEL_H
