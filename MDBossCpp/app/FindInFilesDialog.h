// Find in all documents: one query, every match in every document the files
// tree knows about, in a list you can click.
//
// Modeless, and hidden rather than destroyed when it is closed, so the
// results survive going away to read one of them -- which is the whole point
// of a results list, and what the tree's Contents filter cannot offer.
//
// The query box is deliberately MULTI-LINE: the command exists to find a
// block of text, and a block that has been pasted out of one document has to
// be able to go into the box as it stands.  Enter inserts a line break;
// Ctrl+Enter and the Search button run the search.

#ifndef MDBOSS_APP_FIND_IN_FILES_DIALOG_H
#define MDBOSS_APP_FIND_IN_FILES_DIALOG_H

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FileScan.h"
#include "TextSearch.h"

namespace mdboss {

class FindInFilesDialog : public wxDialog {
public:
    explicit FindInFilesDialog(wxWindow* parent);
    ~FindInFilesDialog() override;

    // Show it, raise it if it is already showing, and seed the query when
    // `initial` is not empty.
    void open(const wxString& initial);

    // Where the documents to search come from.  A provider rather than a list
    // because the tree rescans: the answer has to be taken when the search
    // starts, not when the dialog was built.
    void set_documents(std::function<std::vector<std::string>()> provider)
    {
        documents_ = std::move(provider);
    }
    // Activating a row.  The needle and options go with it so the frame can
    // find the match again in the document it opens -- the file may have
    // changed since the search, and an offset alone would then point at the
    // wrong place with nothing to notice it.
    void set_on_open_match(
        std::function<void(const DocumentMatch&, const std::string&,
                           const SearchOptions&)> handler)
    {
        on_open_match_ = std::move(handler);
    }

private:
    void start_search();
    void show_results(const DocumentSearch& results);
    void on_activated(wxListEvent& event);
    void on_selection_changed(wxListEvent& event);
    void on_char_hook(wxKeyEvent& event);

    wxTextCtrl* query_ = nullptr;
    wxCheckBox* case_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxStaticText* path_ = nullptr;
    wxListCtrl* list_ = nullptr;

    // The results on screen, and the query they answer.
    std::vector<DocumentMatch> matches_;
    std::string searched_for_;
    SearchOptions options_;

    // Reading every document cannot happen on the UI thread; see CLAUDE.md.
    // Same generation-stamped shape as the tree's scan and content search: the
    // counter is atomic because the worker polls it mid-walk to notice it has
    // been superseded, and `alive_` lets the completion lambda tell that this
    // dialog is gone.
    std::shared_ptr<std::atomic<bool>> alive_;
    std::atomic<unsigned> generation_{0};

    std::function<std::vector<std::string>()> documents_;
    std::function<void(const DocumentMatch&, const std::string&,
                       const SearchOptions&)>
        on_open_match_;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_FIND_IN_FILES_DIALOG_H
