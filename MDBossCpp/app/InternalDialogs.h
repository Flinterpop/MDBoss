// The three "add an entry" dialogs behind the Lists menu: a login record, a
// to-do item, and a Grail Diary entry.
//
// Each is a plain modal form that collects text and hands it back; none of
// them touches the filesystem.  Writing is InternalNotes' job, and keeping the
// split means the formatting can be unit-tested without a GUI.

#ifndef MDBOSS_APP_INTERNAL_DIALOGS_H
#define MDBOSS_APP_INTERNAL_DIALOGS_H

#include <wx/dialog.h>
#include <wx/textctrl.h>

#include <string>

#include "InternalNotes.h"

namespace mdboss {

// Name / Link / Login / PW / Last Changed / Notes, one row of logins.md.
//
// The password field is masked, with a "Show" box beside it: typing a password
// you cannot see into a form you are about to save is how the wrong string
// gets stored, and there is no way to check it afterwards short of opening the
// file.  wxTE_PASSWORD cannot be toggled on an existing control, so the field
// is rebuilt when the box changes -- see the .cpp.
class LoginDialog : public wxDialog {
public:
    explicit LoginDialog(wxWindow* parent);

    LoginRecord record() const;

private:
    void rebuild_password_field(bool masked);

    wxTextCtrl* name_ = nullptr;
    wxTextCtrl* link_ = nullptr;
    wxTextCtrl* login_ = nullptr;
    wxTextCtrl* password_ = nullptr;
    wxTextCtrl* changed_ = nullptr;
    wxTextCtrl* notes_ = nullptr;
    wxWindow* password_host_ = nullptr;   // the sizer's owner, for the rebuild
};

// One to-do item.  A single line: the file is a checklist, and a checklist
// item that wraps onto a second line stops being a checklist item.
class TodoDialog : public wxDialog {
public:
    explicit TodoDialog(wxWindow* parent);

    std::string item() const;

private:
    wxTextCtrl* item_ = nullptr;
};

// A Grail Diary entry: free Markdown, multi-line, saved under a dated
// heading.  Unlike the other two nothing is flattened or escaped -- Markdown
// in, Markdown out.
class DiaryDialog : public wxDialog {
public:
    explicit DiaryDialog(wxWindow* parent);

    std::string markdown() const;

private:
    wxTextCtrl* body_ = nullptr;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_INTERNAL_DIALOGS_H
