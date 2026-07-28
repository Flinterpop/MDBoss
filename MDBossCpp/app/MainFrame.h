// The application window: source editor beside a live preview.
//
// This is the first slice of the C++ port's UI.  The file tree, outline,
// recents and favorites panes come next; what is here is the spine everything
// else hangs off -- open, edit, render, save -- so it is worth getting the
// render and scroll-sync behaviour right before adding panes around it.

#ifndef MDBOSS_APP_MAIN_FRAME_H
#define MDBOSS_APP_MAIN_FRAME_H

#include <wx/frame.h>
#include <wx/splitter.h>
#include <wx/stc/stc.h>
#include <wx/timer.h>

#include <string>

#include "Config.h"
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
    void on_open_templates_folder(wxCommandEvent& event);
    void on_refresh(wxCommandEvent& event);
    void on_toggle_files(wxCommandEvent& event);
    void on_toggle_outline(wxCommandEvent& event);
    void on_toggle_editor(wxCommandEvent& event);
    void on_file_types(wxCommandEvent& event);
    void on_help(wxCommandEvent& event);
    void on_about(wxCommandEvent& event);
    void on_toggle_favorite(wxCommandEvent& event);
    void refresh_lists();
    void on_text_changed(wxStyledTextEvent& event);
    void on_editor_scrolled(wxStyledTextEvent& event);
    void on_render_timer(wxTimerEvent& event);
    void on_close(wxCloseEvent& event);

    void render_preview();
    void update_title();
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

    std::string current_path_;
    bool dirty_ = false;
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
