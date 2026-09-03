// The Find bar: a strip along the bottom of the window that searches the
// document in the editor.
//
// A bar rather than a floating dialog, and at the bottom of the FRAME rather
// than inside the editor pane.  The frame's one child is a nest of five
// splitters -- the toggles Unsplit() the editor by pointer -- so wrapping the
// editor in a panel to make room for a bar would have meant editing every one
// of those, for no gain the user can see.  Adding a second row to the frame's
// sizer touches nothing else.
//
// It knows nothing about the editor.  It reports what was typed and which way
// to go; the frame owns the document and decides what that means, the same
// arrangement every other pane here uses.

#ifndef MDBOSS_APP_FIND_BAR_H
#define MDBOSS_APP_FIND_BAR_H

#include <wx/checkbox.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <functional>
#include <string>
#include <utility>

#include "TextSearch.h"

namespace mdboss {

// What the bar is asking for.
struct FindRequest {
    std::string needle;
    SearchOptions options;
    // +1 for the next match, -1 for the previous one.
    int direction = 1;
    // The query itself changed, so the search starts from where the bar was
    // opened rather than from the current selection.  Without it every
    // keystroke would step forward through the document, and deleting a
    // character would never come back.
    bool incremental = false;
};

class FindBar : public wxPanel {
public:
    explicit FindBar(wxWindow* parent);

    // Show the bar and put the caret in it.  `initial` replaces the query
    // when it is not empty -- the editor's selection, so Ctrl+F on a selected
    // word searches for that word, as every editor does.
    void open(const wxString& initial);
    // Hide it and hand focus back, through on_close_.
    void close();

    // What the frame writes after a search: "3 of 12", "Not found".  Cleared
    // whenever the query changes, so a stale count is never read as current.
    void set_status(const wxString& text);

    std::string needle() const;
    SearchOptions options() const;

    void set_on_find(std::function<void(const FindRequest&)> handler)
    {
        on_find_ = std::move(handler);
    }
    void set_on_close(std::function<void()> handler)
    {
        on_close_ = std::move(handler);
    }

private:
    // Raised for a button, for Enter, and for every keystroke in the query.
    void search(int direction, bool incremental);
    void on_char_hook(wxKeyEvent& event);

    wxTextCtrl* query_ = nullptr;
    wxCheckBox* case_ = nullptr;
    wxStaticText* status_ = nullptr;
    std::function<void(const FindRequest&)> on_find_;
    std::function<void()> on_close_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FIND_BAR_H
