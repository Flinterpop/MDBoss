// One window per user session.
//
// As the handler for .md files, MD Boss is launched once per double-clicked
// document.  Without this each launch would be a separate process, and the
// last one to close would overwrite the others' recents, favorites and window
// layout -- which is exactly the bug the Python app's named pipe prevents.
//
// This uses a window property plus WM_COPYDATA rather than a pipe or DDE: it
// needs no thread and no server object, and it rides the message loop the app
// already runs.
//
// A window property alone is not enough, because it cannot be set until there
// IS a window.  Two launches in the same instant therefore both looked around,
// both found nothing, and both opened -- and the installer reproduces exactly
// that every time: its [Run] entry launches the app while Inno's `isreadme`
// flag opens README.md, which this app is the registered handler for.  Two
// live instances is the one thing this file exists to prevent, because both
// write config.json when they close and the last one silently discards the
// other's layout, expanded folders and recents.
//
// So the slot is claimed with a NAMED MUTEX at process start, before any
// window exists, and the second process then WAITS for the first one's window
// to appear before handing the document over.  The wait is bounded: if the
// window never shows up, the caller opens its own rather than exiting and
// losing the user's document.
//
// The property name is deliberately NOT the Python app's pipe name. The two
// builds are a parity pair meant to be run alternately during development, and
// sharing an instance channel would mean launching one silently handed the
// document to the other.

#ifndef MDBOSS_APP_SINGLE_INSTANCE_H
#define MDBOSS_APP_SINGLE_INSTANCE_H

#include <string>

namespace mdboss {

// Mark `hwnd` (as a void* HWND) as this session's MD Boss window.
void mark_as_instance(void* hwnd);

// Hand `path` to an already-running instance and raise it.  An empty path
// just raises.  Returns false if this process should carry on and open its own
// window -- either because it is the first instance, or because the one that
// claimed the slot never produced a window to hand to.
//
// Call this BEFORE creating any window: claiming the slot is the first thing
// it does, and that ordering is what closes the race.
bool forward_to_running(const std::string& path);

// The payload id used by WM_COPYDATA, so a stray message from something else
// is ignored rather than opened.
unsigned long instance_message_id();

}  // namespace mdboss

#endif  // MDBOSS_APP_SINGLE_INSTANCE_H
