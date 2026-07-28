// The outline pane: the current document's headings, indented by level.
// Activating one scrolls the preview to that heading's anchor.

#ifndef MDBOSS_APP_OUTLINE_PANEL_H
#define MDBOSS_APP_OUTLINE_PANEL_H

#include <wx/listbox.h>
#include <wx/panel.h>

#include <functional>
#include <string>
#include <vector>

#include "mdrender/MdRender.h"

namespace mdboss {

class OutlinePanel : public wxPanel {
public:
    explicit OutlinePanel(wxWindow* parent);

    void set_headings(const std::vector<mdrender::Heading>& headings);

    // Called with the heading's slug, which is also its anchor id in the
    // rendered HTML -- the two come from one pass, so they always agree.
    void set_on_activate(std::function<void(const std::string&)> handler)
    {
        on_activate_ = std::move(handler);
    }

private:
    void on_selected(wxCommandEvent& event);

    wxListBox* list_ = nullptr;
    std::vector<std::string> slugs_;
    std::function<void(const std::string&)> on_activate_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_OUTLINE_PANEL_H
