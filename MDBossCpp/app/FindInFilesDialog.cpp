#include "FindInFilesDialog.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/filename.h>
#include <wx/sizer.h>

#include <cassert>
#include <thread>

namespace mdboss {
namespace {

constexpr int kColDocument = 0;
constexpr int kColLine = 1;
constexpr int kColText = 2;

}  // namespace

FindInFilesDialog::FindInFilesDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"Find in all documents", wxDefaultPosition,
               wxSize(880, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      alive_(std::make_shared<std::atomic<bool>>(true))
{
    auto* prompt = new wxStaticText(
        this, wxID_ANY,
        L"Text to find. A block may span several lines — Enter starts a "
        L"new line, Ctrl+Enter searches.");
    query_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(-1, 64), wxTE_MULTILINE);
    case_ = new wxCheckBox(this, wxID_ANY, L"Match &case");
    auto* search = new wxButton(this, wxID_ANY, L"&Search");
    auto* close = new wxButton(this, wxID_CANCEL, L"Close");
    status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->AppendColumn(L"Document", wxLIST_FORMAT_LEFT, 220);
    list_->AppendColumn(L"Line", wxLIST_FORMAT_RIGHT, 60);
    list_->AppendColumn(L"Match", wxLIST_FORMAT_LEFT, 560);

    // The selected row's folder, which is the only place there is room for it:
    // two documents of the same name under different roots are otherwise
    // indistinguishable in the list.
    path_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);

    auto* controls = new wxBoxSizer(wxHORIZONTAL);
    controls->Add(case_, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 12);
    controls->Add(search, 0, wxRIGHT, 6);
    controls->Add(close, 0, wxRIGHT, 12);
    controls->Add(status_, 1, wxALIGN_CENTRE_VERTICAL);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(prompt, 0, wxLEFT | wxRIGHT | wxTOP, 10);
    sizer->Add(query_, 0, wxEXPAND | wxALL, 10);
    sizer->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    sizer->Add(list_, 1, wxEXPAND | wxALL, 10);
    sizer->Add(path_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(sizer);

    search->SetDefault();
    search->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { start_search(); });
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &FindInFilesDialog::on_activated,
                this);
    list_->Bind(wxEVT_LIST_ITEM_SELECTED,
                &FindInFilesDialog::on_selection_changed, this);
    Bind(wxEVT_CHAR_HOOK, &FindInFilesDialog::on_char_hook, this);

    // Closed means hidden, never destroyed: the frame holds this pointer, and
    // the results are worth keeping while the user goes off to read one.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto()) {
            event.Veto();
            Hide();
            return;
        }
        event.Skip();   // the application really is going away
    });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); }, wxID_CANCEL);
}

FindInFilesDialog::~FindInFilesDialog()
{
    // A detached worker may still be running; this is how its completion
    // lambda tells that everything it was about to touch has gone.
    alive_->store(false);
}

void FindInFilesDialog::open(const wxString& initial)
{
    if (!initial.empty()) {
        query_->ChangeValue(initial);
    }
    Show();
    Raise();
    query_->SetFocus();
    query_->SelectAll();
}

void FindInFilesDialog::on_char_hook(wxKeyEvent& event)
{
    const int key = event.GetKeyCode();
    if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && event.ControlDown()) {
        start_search();
        return;
    }
    if (key == WXK_ESCAPE) {
        Hide();
        return;
    }
    event.Skip();
}

void FindInFilesDialog::start_search()
{
    // The box holds what the user typed, newlines and all.  Only the trailing
    // break a text control leaves behind is dropped -- a block pasted in
    // usually ends with one, and searching for it would find nothing in a
    // document whose last line differs.
    std::string needle = query_->GetValue().utf8_string();
    while (!needle.empty() &&
           (needle.back() == '\n' || needle.back() == '\r')) {   // bounded
        needle.pop_back();
    }
    if (needle.size() < kMinSearchNeedle) {
        status_->SetLabel(L"Type at least two characters to search for.");
        return;
    }
    if (!documents_) {
        return;
    }
    const std::vector<std::string> paths = documents_();
    if (paths.empty()) {
        status_->SetLabel(
            L"No documents to search yet — the folder scan has not "
            L"finished.");
        return;
    }

    matches_.clear();
    list_->DeleteAllItems();
    path_->SetLabel(wxEmptyString);
    searched_for_ = needle;
    options_ = SearchOptions{};
    options_.case_sensitive = case_->GetValue();
    status_->SetLabel(wxString::Format(L"Searching %ld documents…",
                                       static_cast<long>(paths.size())));

    const unsigned generation = ++generation_;
    std::shared_ptr<std::atomic<bool>> alive = alive_;
    const SearchOptions options = options_;

    std::thread([this, paths, needle, options, generation, alive] {
        DocumentSearch results;
        // Nothing may escape a detached thread: an uncaught exception is
        // std::terminate, a hard crash with no message.
        try {
            results = search_documents(
                paths, needle, options, [this, generation, alive] {
                    return !alive->load() || generation != generation_.load();
                });
        } catch (...) {
            results = DocumentSearch{};
        }
        wxTheApp->CallAfter([this, results, generation, alive] {
            if (!alive->load() || generation != generation_.load()) {
                return;   // a later search has replaced this one
            }
            show_results(results);
        });
    }).detach();
}

void FindInFilesDialog::show_results(const DocumentSearch& results)
{
    matches_ = results.matches;
    list_->DeleteAllItems();
    for (std::size_t i = 0; i < matches_.size(); ++i) {   // bounded by the cap
        const DocumentMatch& match = matches_[i];
        const wxString full = wxString::FromUTF8(match.path);
        const long row = list_->InsertItem(static_cast<long>(i), wxEmptyString);
        list_->SetItem(row, kColDocument, wxFileName(full).GetFullName());
        list_->SetItem(row, kColLine, wxString::Format(L"%d", match.line));
        list_->SetItem(row, kColText, wxString::FromUTF8(match.text));
    }

    // The list is indexed by row when a result is activated, so a row that
    // does not line up with a match opens the wrong document -- which would
    // look like a search bug rather than a list one.
    assert(static_cast<std::size_t>(list_->GetItemCount()) ==
               matches_.size() &&
           "every match must have exactly one row");

    if (matches_.empty()) {
        status_->SetLabel(wxString::Format(
            L"No matches in %ld documents.",
            static_cast<long>(results.documents_searched)));
        return;
    }
    // Built with a cast per number rather than %zu: wxString::Format goes
    // through wxWidgets' own formatter, not the CRT's, and the size_t length
    // modifier is not part of what it promises to understand.
    wxString summary = wxString::Format(
        (matches_.size() == 1) ? L"%ld match in %ld of %ld documents."
                               : L"%ld matches in %ld of %ld documents.",
        static_cast<long>(matches_.size()),
        static_cast<long>(results.documents_matched),
        static_cast<long>(results.documents_searched));
    if (results.truncated) {
        // A bound that can be hit has to be reportable: a capped list
        // presented as the whole answer is worse than a slow one.
        summary += L" (partial — search limit reached)";
    }
    status_->SetLabel(summary);
}

void FindInFilesDialog::on_selection_changed(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= matches_.size()) {
        return;
    }
    path_->SetLabel(wxString::FromUTF8(matches_[static_cast<std::size_t>(row)]
                                           .path));
}

void FindInFilesDialog::on_activated(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= matches_.size() ||
        !on_open_match_) {
        return;
    }
    // Copied out before the call: opening a document can rebuild things that
    // reach back in here, and the row must not be read from a list that has
    // moved on underneath it.
    const DocumentMatch match = matches_[static_cast<std::size_t>(row)];
    on_open_match_(match, searched_for_, options_);
}

}  // namespace mdboss
