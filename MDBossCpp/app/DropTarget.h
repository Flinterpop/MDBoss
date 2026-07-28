// Opening documents dropped onto the window.
//
// A drop opens the file where it lies -- it is not copied anywhere.  That is
// what the Python app settled on: an earlier version ingested drops into
// MD_Inbox, which surprised people who meant "show me this file".
//
// The target has to be attached to several windows, not just the frame: a
// child window under the cursor consumes the drop, so the editor, the tree
// and the preview each need one or dropping on them does nothing.

#ifndef MDBOSS_APP_DROP_TARGET_H
#define MDBOSS_APP_DROP_TARGET_H

#include <wx/dnd.h>

#include <functional>
#include <string>

namespace mdboss {

class DocumentDropTarget : public wxFileDropTarget {
public:
    explicit DocumentDropTarget(std::function<void(const std::string&)> on_drop)
        : on_drop_(std::move(on_drop))
    {
    }

    bool OnDropFiles(wxCoord x, wxCoord y,
                     const wxArrayString& filenames) override;

private:
    std::function<void(const std::string&)> on_drop_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_DROP_TARGET_H
