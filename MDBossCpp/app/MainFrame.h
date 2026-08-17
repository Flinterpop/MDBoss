// The application window: file tree, outline, source editor and live preview.
//
// This class is deliberately the only place that knows about all of them.  The
// panes are self-contained and report what happened through std::function
// hooks rather than reaching for the frame, so each can be read on its own;
// what they cannot do is decide policy.  Whether a changed file is reloaded,
// whether a document may be discarded, where a new file lands -- that lives
// here, because those answers depend on state no single pane holds.

#ifndef MDBOSS_APP_MAIN_FRAME_H
#define MDBOSS_APP_MAIN_FRAME_H

#include <wx/frame.h>
#include <wx/splitter.h>
#include <wx/stc/stc.h>
#include <wx/timer.h>

#include <string>

#include "Config.h"
#include "DocumentWatcher.h"
#include "Updater.h"
#include "FileTreePanel.h"
#include "OutlinePanel.h"
#include "PathListPanel.h"
#include "PreviewPane.h"

namespace mdboss {

class MainFrame : public wxFrame {
public:
    MainFrame();

    // Open `path`, replacing the current document.  Returns false and leaves
    // the current document intact if the file cannot be read.
    bool open_path(const std::string& path);

    // WM_COPYDATA carries a document path from a second launch; see
    // SingleInstance.h for why there is only ever one window.
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wparam,
                            WXLPARAM lparam) override;

private:
    void build_menu();
    void build_toolbar();
    void build_panes();
    void bind_events();

    void on_open(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_exit(wxCommandEvent& event);
    void on_toggle_front_matter(wxCommandEvent& event);
    void on_manage_folders(wxCommandEvent& event);
    void on_new(wxCommandEvent& event);
    void on_new_from_template(wxCommandEvent& event);
    // Close the document without closing the window: the editor and preview
    // go back to empty and no file is open.
    void on_close_document(wxCommandEvent& event);
    // Insert one of the fixed snippet blocks at the caret; see kSnippets.
    void on_snippet(wxCommandEvent& event);
    // Pick an image file and insert a Markdown reference to it.
    void on_insert_image(wxCommandEvent& event);
    // Drop `body` in at the caret as its own block, adding the blank lines
    // around it that make Markdown treat it as one.
    void insert_block(const std::string& body);
    void on_open_templates_folder(wxCommandEvent& event);
    // The three MD_Internal lists, and a way to reach the folder itself.
    void on_add_login(wxCommandEvent& event);
    void on_add_todo(wxCommandEvent& event);
    void on_add_diary(wxCommandEvent& event);
    void on_open_internal_folder(wxCommandEvent& event);
    // Rebuild MD_Internal\TechNotes.md from the documents on disk and open it.
    void on_tech_notes(wxCommandEvent& event);
    // Export the rendered preview to PDF via WebView2's own print pipeline.
    void on_export_pdf(wxCommandEvent& event);
    // Switch the preview stylesheet (View menu). Persists and re-renders.
    void on_preview_theme(wxCommandEvent& event);
    // A document moved on disk (dragged in the tree, or renamed): rewrite the
    // absolute paths held in favorites and recents, and follow it if it is the
    // document currently open.
    void on_path_moved(const std::string& from, const std::string& to);
    // Shared tail of the three: append, report a failure, refresh the tree.
    void save_internal_entry(const std::string& filename,
                             const std::string& seed,
                             const std::string& block, const wxString& what);
    void on_refresh(wxCommandEvent& event);
    void on_toggle_files(wxCommandEvent& event);
    void on_toggle_outline(wxCommandEvent& event);
    void on_toggle_editor(wxCommandEvent& event);
    // Hide the rendered preview, leaving the editor the whole pane.  Shares
    // one splitter with the editor, so the two toggles are not independent:
    // hiding both would leave nothing, and either toggle restores the pair.
    void on_toggle_preview(wxCommandEvent& event);
    void on_file_types(wxCommandEvent& event);
    void on_help(wxCommandEvent& event);
    void on_about(wxCommandEvent& event);
    void on_check_updates(wxCommandEvent& event);
    // Download the right asset for this copy -- the installer, or the
    // portable zip when no uninstaller sits beside the exe -- then hand over
    // to a batch and exit.  Split in two because the download is
    // asynchronous: the second half runs from its completion callback.
    void install_update(const ReleaseInfo& info, bool portable);
    void hand_off_to_installer(const std::string& setup_path);
    void hand_off_to_portable(const std::string& zip_path);
    // Shared tail of both hand-offs: write the batch, spawn it hidden, and
    // close the app so the batch's wait loop can finish.
    void spawn_handoff_and_close(const std::string& batch_path,
                                 const std::string& text);
    void on_toggle_favorite(wxCommandEvent& event);
    void on_export_favorites();
    void on_import_favorites();
    // Copy chosen Markdown files into MD_Inbox and open the first, which is
    // what makes the command feel like an import rather than a file copy.
    void on_import_to_inbox();
    void refresh_lists();
    void on_text_changed(wxStyledTextEvent& event);
    void on_editor_scrolled(wxStyledTextEvent& event);
    void on_render_timer(wxTimerEvent& event);
    // Drops the guard on the preview's scroll echo once it can no longer be
    // ours; see kScrollEchoMs.
    void on_scroll_echo_timer(wxTimerEvent& event);
    void on_close(wxCloseEvent& event);

    void render_preview();
    void update_title();
    // Back to no document open.  Asks nothing -- the caller decides whether
    // unsaved work needs confirming first.
    void clear_document();
    // Grey the Close command out when there is nothing to close.  Called from
    // update_title(), which every document-state change already ends in.
    void update_close_enabled();
    // Keep the View menu item and the toolbar button showing the same state:
    // they are two check controls sharing one id and wx syncs neither.
    void sync_front_matter_checks(bool hide);
    // Record which panes are showing, the moment it changes.
    void save_pane_visibility();
    // The configured root folders, as plain paths.
    std::vector<std::string> root_paths() const;
    // The open document changed underneath us.  Never clobbers unsaved work:
    // a modified buffer is kept and the user told, because the edits in the
    // editor are the only copy of themselves and the file on disk is not.
    void on_document_changed(const std::string& path, bool still_exists);
    void reload_from_disk();
    bool save_to(const std::string& path);
    bool confirm_discard();
    void sync_preview_from_editor();
    void on_preview_scrolled(double ratio);

    Config config_;
    // Left column is Recent over Favorites over Files; that column then sits
    // left of Outline, which sits left of the editor/preview pair.  Five
    // splitters, because wxSplitterWindow only ever holds two panes.
    wxSplitterWindow* files_split_ = nullptr;
    wxSplitterWindow* recent_split_ = nullptr;
    wxSplitterWindow* favorites_split_ = nullptr;
    wxSplitterWindow* outline_split_ = nullptr;
    wxSplitterWindow* split_ = nullptr;
    PathListPanel* recent_ = nullptr;
    PathListPanel* favorites_ = nullptr;
    FileTreePanel* files_ = nullptr;
    OutlinePanel* outline_ = nullptr;
    wxStyledTextCtrl* editor_ = nullptr;
    PreviewPane* preview_ = nullptr;
    wxTimer render_timer_;
    wxTimer scroll_echo_timer_;
    DocumentWatcher watcher_;

    std::string current_path_;
    bool dirty_ = false;
    // Set once the installer has been handed the job, so the close that
    // follows does not ask about unsaved work a second time.
    bool updating_ = false;
    // Sash positions remembered across a hide, so showing a pane again
    // restores the width the user had chosen rather than a default.
    int hidden_files_sash_ = 0;
    int hidden_outline_sash_ = 0;
    int hidden_editor_sash_ = 0;
    // Scroll sync echoes: a programmatic scroll on one side raises the same
    // event a user scroll would, so each side ignores movement it caused.
    bool suppress_editor_scroll_ = false;
    bool suppress_preview_scroll_ = false;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_MAIN_FRAME_H
