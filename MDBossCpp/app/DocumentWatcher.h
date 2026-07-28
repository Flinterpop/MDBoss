// Notices when the open document is changed by something other than us.
//
// A deliberate divergence from the Python app, which has no watcher at all:
// its _refresh_watcher() (app.py) only records root paths -- the set it fills
// is never read -- and its comment says recursive watching was avoided on
// purpose, with F5 as the rescan.  That decision is respected here.  Only the
// ONE OPEN DOCUMENT is watched; the tree still refreshes on demand, so this
// adds no cost that scales with how many folders are configured.

#ifndef MDBOSS_APP_DOCUMENT_WATCHER_H
#define MDBOSS_APP_DOCUMENT_WATCHER_H

#include <wx/event.h>
#include <wx/fswatcher.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <string>

#include "FileScan.h"

namespace mdboss {

class DocumentWatcher : public wxEvtHandler {
public:
    // `path` is the document that changed; `still_exists` is false when it
    // was deleted or renamed away, which the frame must not treat as "reload"
    // -- there would be nothing to reload from.
    using Callback = std::function<void(const std::string& path,
                                        bool still_exists)>;

    explicit DocumentWatcher(Callback on_changed);

    // Watch `path`, dropping any previous watch.  An empty path just stops.
    // Idempotent: re-watching the current path only re-reads its stamp, which
    // is what makes it safe for callers to call this after every save.
    void watch(const std::string& path);

    // Take the file's current state to be the expected one.  Called after we
    // write the file ourselves, so the event our own save raises is not
    // reported back to us as an outside change.
    void accept_current_state();

private:
    void on_fs_event(wxFileSystemWatcherEvent& event);
    void on_settled(wxTimerEvent& event);
    bool concerns_us(const wxFileName& name) const;

    Callback on_changed_;
    // By pointer because a wxFileSystemWatcher cannot be re-targeted: moving
    // to another document destroys this one and builds another.
    std::unique_ptr<wxFileSystemWatcher> watcher_;
    // Owned by this handler rather than the frame, which sidesteps the wx
    // trap that a timer constructed with an owner posts its events to that
    // owner -- two timers on one frame need distinct ids to stay apart.
    wxTimer settle_;
    std::string path_;
    wxString filename_;
    FileStamp known_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_DOCUMENT_WATCHER_H
