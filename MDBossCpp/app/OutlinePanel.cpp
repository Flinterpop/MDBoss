#include "OutlinePanel.h"

#include <wx/sizer.h>

#include <cassert>

namespace mdboss {

OutlinePanel::OutlinePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    list_ = new wxListBox(this, wxID_ANY);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(list_, 1, wxEXPAND);
    SetSizer(sizer);

    list_->Bind(wxEVT_LISTBOX, &OutlinePanel::on_selected, this);
}

void OutlinePanel::set_headings(const std::vector<mdrender::Heading>& headings)
{
    // Rebuilding on every render would fight the user's selection, so bail if
    // nothing changed -- the common case while typing body text.
    std::vector<std::string> slugs;
    slugs.reserve(headings.size());
    for (const mdrender::Heading& heading : headings) {
        slugs.push_back(heading.slug);
    }
    if (slugs == slugs_) {
        return;
    }
    slugs_ = std::move(slugs);

    wxArrayString labels;
    for (const mdrender::Heading& heading : headings) {
        assert(heading.level >= 1 && "heading level is 1-based");
        const int indent = (heading.level - 1) * 2;
        labels.Add(wxString(' ', static_cast<std::size_t>(indent)) +
                   wxString::FromUTF8(heading.text));
    }
    list_->Set(labels);
}

void OutlinePanel::on_selected(wxCommandEvent& event)
{
    const int index = list_->GetSelection();
    if (index >= 0 && static_cast<std::size_t>(index) < slugs_.size() &&
        on_activate_) {
        on_activate_(slugs_[static_cast<std::size_t>(index)]);
    }
    event.Skip();
}

}  // namespace mdboss
