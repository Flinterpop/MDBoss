// A titled list of document paths, used for both Recent and Favorites.
//
// Rows show the filename only, with the full path as the tooltip, and a
// missing file is drawn in red rather than silently opening nothing --
// matching the Python app.  wxListCtrl rather than wxListBox because only the
// former can colour an individual row.

#ifndef MDBOSS_APP_PATH_LIST_PANEL_H
#define MDBOSS_APP_PATH_LIST_PANEL_H

#include <wx/listctrl.h>
#include <wx/panel.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mdboss {

class PathListPanel : public wxPanel {
public:
    // `extra_label` names the list-specific context-menu action -- "Remove
    // from favorites", "Add to favorites" -- or is empty for none.
    PathListPanel(wxWindow* parent, const wxString& title,
                  const wxString& extra_label);

    void set_paths(const std::vector<std::string>& paths);

    void set_on_activate(std::function<void(const std::string&)> handler)
    {
        on_activate_ = std::move(handler);
    }
    // Invoked for the list-specific action named by `extra_label`.
    void set_on_extra(std::function<void(const std::string&)> handler)
    {
        on_extra_ = std::move(handler);
    }
    void set_on_clear(std::function<void()> handler)
    {
        on_clear_ = std::move(handler);
    }

    // A command appended to the context menu above "Clear list", for actions
    // that belong to the list as a whole rather than to the row under the
    // cursor.  Favorites uses these for import/export; Recent has none, which
    // is why they are added per instance rather than built in.
    void add_menu_command(const wxString& label, std::function<void()> handler);

private:
    // Single click opens; see the .cpp for why this is a mouse event and not
    // wxEVT_LIST_ITEM_SELECTED.
    void on_left_click(wxMouseEvent& event);
    void on_activated(wxListEvent& event);
    void on_context_menu(wxListEvent& event);
    std::string selected_path() const;

    wxListCtrl* list_ = nullptr;
    wxString extra_label_;
    std::vector<std::string> paths_;
    std::function<void(const std::string&)> on_activate_;
    std::function<void(const std::string&)> on_extra_;
    std::function<void()> on_clear_;
    std::vector<std::pair<wxString, std::function<void()>>> commands_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_PATH_LIST_PANEL_H
