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
#include "PreviewPane.h"

namespace mdboss {

class MainFrame : public wxFrame {
public:
    MainFrame();

    // Open `path`, replacing the current document.  Returns false and leaves
    // the current document intact if the file cannot be read.
    bool open_path(const std::string& path);

private:
    void build_menu();
    void build_panes();
    void bind_events();

    void on_open(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_exit(wxCommandEvent& event);
    void on_toggle_front_matter(wxCommandEvent& event);
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
    wxSplitterWindow* split_ = nullptr;
    wxStyledTextCtrl* editor_ = nullptr;
    PreviewPane* preview_ = nullptr;
    wxTimer render_timer_;

    std::string current_path_;
    bool dirty_ = false;
    // Scroll sync echoes: a programmatic scroll on one side raises the same
    // event a user scroll would, so each side ignores movement it caused.
    bool suppress_editor_scroll_ = false;
    bool suppress_preview_scroll_ = false;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_MAIN_FRAME_H
