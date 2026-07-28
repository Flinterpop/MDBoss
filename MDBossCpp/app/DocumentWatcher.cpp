#include "DocumentWatcher.h"

#include <cassert>
#include <filesystem>

#include "PathUtf8.h"

namespace mdboss {
namespace {

namespace fs = std::filesystem;

// One save is not one event.  Windows raises several notifications for a
// single write -- and an editor that saves atomically raises a create, a
// rename and a delete -- so the file is only examined once the noise stops.
constexpr int kSettleMs = 300;

}  // namespace

DocumentWatcher::DocumentWatcher(Callback on_changed)
    : on_changed_(std::move(on_changed)),
      settle_(this)
{
    assert(on_changed_ && "DocumentWatcher needs a callback");
    // Bound once here rather than per watch(): Bind() accumulates handlers,
    // so binding on every document change would fire the handler N times.
    Bind(wxEVT_FSWATCHER, &DocumentWatcher::on_fs_event, this);
    Bind(wxEVT_TIMER, &DocumentWatcher::on_settled, this);
}

void DocumentWatcher::watch(const std::string& path)
{
    if (!path.empty() && path == path_ && watcher_) {
        // Same document: keep the watch, just re-read what we expect to see.
        accept_current_state();
        return;
    }

    settle_.Stop();
    watcher_.reset();
    path_.clear();
    filename_.clear();
    known_ = FileStamp{};
    if (path.empty()) {
        return;
    }

    const fs::path target = path_from_utf8(path);
    const fs::path dir = target.parent_path();
    if (dir.empty() || target.filename().empty()) {
        return;   // Not a path we can locate a parent for; watch nothing.
    }

    path_ = path;
    filename_ = wxString(target.filename().wstring());
    known_ = stamp_of(path);

    auto watcher = std::make_unique<wxFileSystemWatcher>();
    watcher->SetOwner(this);
    // The DIRECTORY is watched, not the file.  Many editors save by writing a
    // temporary file and renaming it over the target, which replaces the
    // directory entry entirely; a watch on the file itself follows the old
    // entry out of existence and then never fires again.  Watching the parent
    // sees the rename land.  Add(), not AddTree() -- one directory, no
    // recursion, so a document in a huge folder costs no more than any other.
    const wxFileName dir_name = wxFileName::DirName(wxString(dir.wstring()));
    const bool added = watcher->Add(dir_name, wxFSW_EVENT_CREATE |
                                                  wxFSW_EVENT_DELETE |
                                                  wxFSW_EVENT_RENAME |
                                                  wxFSW_EVENT_MODIFY);
    if (!added) {
        // Some locations cannot be watched at all (a few network shares).
        // Failing quietly leaves the app exactly as it behaved before there
        // was a watcher, which is a working app; F5 and reopening still work.
        return;
    }
    watcher_ = std::move(watcher);
}

void DocumentWatcher::accept_current_state()
{
    if (path_.empty()) {
        return;
    }
    known_ = stamp_of(path_);
}

bool DocumentWatcher::concerns_us(const wxFileName& name) const
{
    if (filename_.empty()) {
        return false;
    }
    // Windows paths are case-insensitive, and the case reported by the
    // notification need not match the case the document was opened under.
    return name.GetFullName().IsSameAs(filename_, false);
}

void DocumentWatcher::on_fs_event(wxFileSystemWatcherEvent& event)
{
    if (path_.empty()) {
        return;
    }
    // Watching the directory means most events are about neighbouring files.
    // A rename carries two paths and either may be ours: renaming onto our
    // name replaces the document, renaming away from it removes the document.
    if (!concerns_us(event.GetPath()) && !concerns_us(event.GetNewPath())) {
        return;
    }
    settle_.Start(kSettleMs, wxTIMER_ONE_SHOT);
}

void DocumentWatcher::on_settled(wxTimerEvent&)
{
    if (path_.empty()) {
        return;
    }
    const FileStamp now = stamp_of(path_);
    if (now == known_) {
        // The echo of our own save, or a write that left the file identical.
        return;
    }
    known_ = now;
    assert(on_changed_ && "callback lost");
    on_changed_(path_, now.exists);
}

}  // namespace mdboss
