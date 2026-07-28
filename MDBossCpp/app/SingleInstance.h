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
// just raises.  Returns false if no instance was found, in which case the
// caller should carry on and open its own window.
bool forward_to_running(const std::string& path);

// The payload id used by WM_COPYDATA, so a stray message from something else
// is ignored rather than opened.
unsigned long instance_message_id();

}  // namespace mdboss

#endif  // MDBOSS_APP_SINGLE_INSTANCE_H
