#include "InternalDialogs.h"

#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>

namespace mdboss {
namespace {

constexpr int kIdShowPassword = wxID_HIGHEST + 120;

// A label above its field, the shape every row of these forms uses.
void add_field(wxWindow* parent, wxSizer* sizer, const wxString& label,
               wxTextCtrl* field)
{
    sizer->Add(new wxStaticText(parent, wxID_ANY, label), 0,
               wxLEFT | wxRIGHT | wxTOP, 8);
    sizer->Add(field, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
}

}  // namespace

// ---------------------------------------------------------------------------
// LoginDialog
// ---------------------------------------------------------------------------

LoginDialog::LoginDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"Add a login record", wxDefaultPosition,
               wxSize(460, -1))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    name_ = new wxTextCtrl(this, wxID_ANY);
    link_ = new wxTextCtrl(this, wxID_ANY);
    login_ = new wxTextCtrl(this, wxID_ANY);
    changed_ = new wxTextCtrl(this, wxID_ANY,
                              wxString::FromUTF8(today_stamp()));
    notes_ = new wxTextCtrl(this, wxID_ANY);

    add_field(this, outer, L"Name", name_);
    add_field(this, outer, L"Link", link_);
    add_field(this, outer, L"Login", login_);

    // Password row: the field plus a Show box.  wxTE_PASSWORD is fixed at
    // creation on MSW, so revealing means building a second control and
    // swapping it in, carrying the text across.
    outer->Add(new wxStaticText(this, wxID_ANY, L"PW"), 0,
               wxLEFT | wxRIGHT | wxTOP, 8);
    password_host_ = this;
    password_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    auto* password_row = new wxBoxSizer(wxHORIZONTAL);
    password_row->Add(password_, 1, wxALIGN_CENTER_VERTICAL);
    auto* show = new wxCheckBox(this, kIdShowPassword, L"Show");
    password_row->Add(show, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    outer->Add(password_row, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    add_field(this, outer, L"Last Changed", changed_);
    add_field(this, outer, L"Notes", notes_);

    // Said plainly on the form rather than buried in the docs: the person
    // typing a password deserves to know where it is about to land.
    outer->Add(new wxStaticLine(this, wxID_ANY), 0,
               wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    auto* warning = new wxStaticText(
        this, wxID_ANY,
        L"Saved as plain text in MD_Internal\\logins.md, and searchable "
        L"like any other document.");
    warning->Wrap(430);
    outer->Add(warning, 0, wxALL, 8);

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (buttons != nullptr) {
        outer->Add(buttons, 0, wxEXPAND | wxALL, 8);
    }

    SetSizerAndFit(outer);
    name_->SetFocus();

    show->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        rebuild_password_field(!event.IsChecked());
    });
}

void LoginDialog::rebuild_password_field(bool masked)
{
    if (password_ == nullptr || password_host_ == nullptr) {
        return;
    }
    const wxString carried = password_->GetValue();
    wxSizer* row = password_->GetContainingSizer();
    if (row == nullptr) {
        return;
    }
    auto* replacement =
        new wxTextCtrl(password_host_, wxID_ANY, carried, wxDefaultPosition,
                       wxDefaultSize, masked ? wxTE_PASSWORD : 0);
    row->Replace(password_, replacement);
    password_->Destroy();
    password_ = replacement;
    password_->SetFocus();
    password_->SetInsertionPointEnd();
    Layout();
}

LoginRecord LoginDialog::record() const
{
    LoginRecord out;
    out.name = name_->GetValue().utf8_string();
    out.link = link_->GetValue().utf8_string();
    out.login = login_->GetValue().utf8_string();
    out.password = password_->GetValue().utf8_string();
    out.last_changed = changed_->GetValue().utf8_string();
    out.notes = notes_->GetValue().utf8_string();
    return out;
}

// ---------------------------------------------------------------------------
// TodoDialog
// ---------------------------------------------------------------------------

TodoDialog::TodoDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"Add a to-do", wxDefaultPosition,
               wxSize(460, -1))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    // wxTE_PROCESS_ENTER so Enter accepts the dialog: this form has one field
    // and reaching for the mouse to confirm it is a poor trade.
    item_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxDefaultSize, wxTE_PROCESS_ENTER);
    add_field(this, outer, L"To do", item_);
    outer->Add(new wxStaticText(
                   this, wxID_ANY,
                   wxString::FromUTF8("Added to MD_Internal\\ToDoList.md, "
                                      "dated " + today_stamp() + ".")),
               0, wxALL, 8);

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (buttons != nullptr) {
        outer->Add(buttons, 0, wxEXPAND | wxALL, 8);
    }

    SetSizerAndFit(outer);
    item_->SetFocus();

    item_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        if (IsModal()) {
            EndModal(wxID_OK);
        }
    });
}

std::string TodoDialog::item() const
{
    return item_->GetValue().utf8_string();
}

// ---------------------------------------------------------------------------
// DiaryDialog
// ---------------------------------------------------------------------------

DiaryDialog::DiaryDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"Add a Grail Diary entry", wxDefaultPosition,
               wxSize(560, 420), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    outer->Add(new wxStaticText(
                   this, wxID_ANY,
                   wxString::FromUTF8("Entry for " + today_stamp() +
                                      ". Markdown is kept as written.")),
               0, wxLEFT | wxRIGHT | wxTOP, 8);

    // No wxTE_PROCESS_ENTER here, unlike the to-do: Enter has to insert a
    // newline in a body that is meant to be several lines of Markdown.
    body_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxDefaultSize, wxTE_MULTILINE);
    outer->Add(body_, 1, wxEXPAND | wxALL, 8);

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (buttons != nullptr) {
        outer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    SetSizer(outer);
    body_->SetFocus();
}

std::string DiaryDialog::markdown() const
{
    return body_->GetValue().utf8_string();
}

// ---------------------------------------------------------------------------
// FactDialog
// ---------------------------------------------------------------------------

FactDialog::FactDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"Add a fact", wxDefaultPosition,
               wxSize(560, -1))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    // Prefilled with today, but editable: a fact is true of some date, which
    // is often not the day it was written down.
    date_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(today_stamp()));
    // wxTE_PROCESS_ENTER on the fact itself, as the to-do has: the common case
    // is one line and a tab-tab-click to confirm is a poor trade.  Single line
    // because the row is a table row; a newline would end it.
    fact_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxDefaultSize, wxTE_PROCESS_ENTER);
    tags_ = new wxTextCtrl(this, wxID_ANY);
    source_ = new wxTextCtrl(this, wxID_ANY);

    add_field(this, outer, L"Date", date_);
    add_field(this, outer, L"Fact", fact_);
    add_field(this, outer, L"Tags", tags_);
    add_field(this, outer, L"Source", source_);

    outer->Add(new wxStaticText(
                   this, wxID_ANY,
                   wxString::FromUTF8(
                       "Added to MD_Internal\\Facts.md. Tags are free text -- "
                       "separate several with commas.")),
               0, wxALL, 8);

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    if (buttons != nullptr) {
        outer->Add(buttons, 0, wxEXPAND | wxALL, 8);
    }

    SetSizerAndFit(outer);
    fact_->SetFocus();

    fact_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        if (IsModal()) {
            EndModal(wxID_OK);
        }
    });
}

FactRecord FactDialog::record() const
{
    FactRecord out;
    out.date = date_->GetValue().utf8_string();
    out.fact = fact_->GetValue().utf8_string();
    out.tags = tags_->GetValue().utf8_string();
    out.source = source_->GetValue().utf8_string();
    return out;
}

}  // namespace mdboss
