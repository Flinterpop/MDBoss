#include "FindBar.h"

#include <wx/button.h>
#include <wx/sizer.h>

namespace mdboss {

FindBar::FindBar(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    auto* label = new wxStaticText(this, wxID_ANY, L"Find:");
    query_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(260, -1), wxTE_PROCESS_ENTER);
    auto* previous = new wxButton(this, wxID_ANY, L"P&revious",
                                  wxDefaultPosition, wxDefaultSize,
                                  wxBU_EXACTFIT);
    auto* next = new wxButton(this, wxID_ANY, L"&Next", wxDefaultPosition,
                              wxDefaultSize, wxBU_EXACTFIT);
    case_ = new wxCheckBox(this, wxID_ANY, L"Match &case");
    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    auto* done = new wxButton(this, wxID_ANY, L"Close", wxDefaultPosition,
                              wxDefaultSize, wxBU_EXACTFIT);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    const int flags = wxALIGN_CENTRE_VERTICAL | wxALL;
    sizer->Add(label, 0, flags, 4);
    sizer->Add(query_, 0, flags, 4);
    sizer->Add(previous, 0, flags, 2);
    sizer->Add(next, 0, flags, 2);
    sizer->Add(case_, 0, flags, 4);
    // The status takes the slack, so the two buttons beside the query do not
    // move as the text under them changes length.
    sizer->Add(status_, 1, flags, 6);
    sizer->Add(done, 0, flags, 4);
    SetSizer(sizer);

    query_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        event.Skip();
        // Typing searches from where the bar was opened, not from the last
        // match: see FindRequest::incremental.
        search(1, true);
    });
    query_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& event) {
        event.Skip();
        search(1, false);
    });
    previous->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        search(-1, false);
    });
    next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { search(1, false); });
    case_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        // The answer to a different question, so it starts over rather than
        // carrying on from wherever the last one landed.
        search(1, true);
    });
    done->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { close(); });

    // CHAR_HOOK rather than KEY_DOWN: it is seen before the text control has
    // consumed the key, which is the only way Escape and Shift+Enter can be
    // read while the caret is in the query box.
    Bind(wxEVT_CHAR_HOOK, &FindBar::on_char_hook, this);
}

void FindBar::on_char_hook(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_ESCAPE) {
        close();
        return;   // deliberately not skipped: Escape belongs to the bar
    }
    if ((event.GetKeyCode() == WXK_RETURN ||
         event.GetKeyCode() == WXK_NUMPAD_ENTER) &&
        event.ShiftDown()) {
        search(-1, false);
        return;
    }
    event.Skip();
}

void FindBar::open(const wxString& initial)
{
    if (!initial.empty()) {
        // ChangeValue, not SetValue: the latter raises wxEVT_TEXT, which would
        // start a search from an origin the frame has not set yet.
        query_->ChangeValue(initial);
    }
    status_->SetLabel(wxEmptyString);
    Show();
    GetParent()->Layout();
    query_->SetFocus();
    query_->SelectAll();
}

void FindBar::close()
{
    Hide();
    GetParent()->Layout();
    if (on_close_) {
        on_close_();
    }
}

void FindBar::set_status(const wxString& text)
{
    status_->SetLabel(text);
    // The status shares a row with controls that do not change size; without
    // this the new text is drawn over the old one.
    Layout();
}

std::string FindBar::needle() const
{
    return std::string(query_->GetValue().utf8_string());
}

SearchOptions FindBar::options() const
{
    SearchOptions options;
    options.case_sensitive = case_->GetValue();
    return options;
}

void FindBar::search(int direction, bool incremental)
{
    if (!on_find_) {
        return;
    }
    FindRequest request;
    request.needle = needle();
    request.options = options();
    request.direction = direction;
    request.incremental = incremental;
    if (request.needle.empty()) {
        status_->SetLabel(wxEmptyString);
        return;
    }
    on_find_(request);
}

}  // namespace mdboss
