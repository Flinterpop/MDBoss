// Per-user settings, shared byte-for-byte with the Python app.
//
// Both apps read and write the same %APPDATA%\MDBoss\config.json, so a user
// can run either against one profile.  That imposes one hard rule:
//
//   *Never drop a key this app does not understand.*
//
// The Python app stores its window layout as base64-encoded Qt blobs
// (`geometry`, `split_main`, `split_left_v2`, `split_right`) which are
// meaningless here and unreconstructable if lost.  Saving therefore merges
// into the file on disk rather than rewriting it, exactly as Python's
// update_config() does, and this app keeps its own layout under separate
// `wx_*` keys so the two never fight over the same value.

#ifndef MDBOSS_APP_CONFIG_H
#define MDBOSS_APP_CONFIG_H

#include <cstddef>
#include <string>
#include <vector>

namespace mdboss {

// Matching app.py's MAX_RECENTS and MAX_FAVORITES.  Public because the
// favorites interchange file is capped on import too, and a second copy of
// the number in another file is a drift waiting to happen.
inline constexpr std::size_t kMaxRecents = 6;
inline constexpr std::size_t kMaxFavorites = 10;

// One root folder as the Python app stores it: {"name": ..., "path": ...}.
struct Root {
    std::string name;
    std::string path;
};

// The per-user data folder, %APPDATA%\MDBoss, shared with the Python app.
// Config and templates both live under it.
std::string user_data_dir();

class Config {
public:
    // Full path to config.json, honouring %APPDATA% like _user_data_base().
    static std::string path();

    // Read from disk.  A missing or malformed file yields defaults rather
    // than an error: settings are a convenience, never load-bearing.
    void load();

    // Merge the known keys back into whatever is on disk now and write it.
    // Returns false if the file could not be written.
    bool save() const;

    const std::vector<Root>& roots() const { return roots_; }
    const std::vector<std::string>& favorites() const { return favorites_; }
    const std::vector<std::string>& recents() const { return recents_; }
    bool hide_front_matter() const { return hide_front_matter_; }

    // Whether a folder is shown as a flat list of every Markdown file beneath
    // it, rather than as a folder tree.  Any folder can be flattened, not only
    // a top-level root.
    //
    // The JSON key stays `wx_flat_roots` even though it now holds subfolder
    // paths too: it shipped under that name, and renaming it would silently
    // discard the choices of anyone upgrading.
    bool is_flat_folder(const std::string& path) const;
    void set_flat_folder(const std::string& path, bool flat);

    // Which preview stylesheet is in use, under a port-only
    // `wx_preview_theme` key: "github" or "notes".  Stored as the name rather
    // than an index so a value written by a later build is readable rather
    // than silently meaning a different theme.
    const std::string& preview_theme() const { return preview_theme_; }
    void set_preview_theme(std::string theme)
    {
        preview_theme_ = std::move(theme);
    }

    // Folders the files scan does not descend into, under a port-only
    // `wx_excluded_folders` key.  A generated folder inside a root -- a build
    // tree, a tile cache -- can hold orders of magnitude more entries than
    // everything the user actually wants indexed, and walking it costs the
    // scan tens of seconds for nothing.  Stored as absolute paths, normalised
    // by the panel that writes them, exactly as `wx_flat_roots` is.
    const std::vector<std::string>& excluded_folders() const
    {
        return excluded_folders_;
    }
    bool is_excluded_folder(const std::string& path) const;
    void set_excluded_folder(const std::string& path, bool excluded);

    // Which folders were expanded in the files tree when the window closed,
    // under a port-only `wx_expanded_folders` key.  Reopening collapsed means
    // clicking back down to the same folder every launch; the Python app never
    // stored this, so the key is ours alone.  Paths are normalised (absolute,
    // lower-cased) by the panel that writes them.
    const std::vector<std::string>& expanded_folders() const
    {
        return expanded_folders_;
    }
    void set_expanded_folders(std::vector<std::string> folders);

    // Which starter templates this profile has already been offered, under a
    // port-only `wx_seeded_templates` key.  Recorded by name so a starter
    // added in a later version reaches a profile whose templates folder was
    // created by an earlier one -- and so deleting a template still means it.
    //
    // knows_seeded_templates() distinguishes "offered nothing yet" from "the
    // key predates this mechanism", which is what lets seed_templates() adopt
    // an existing folder's starters instead of writing them a second time.
    bool knows_seeded_templates() const { return seeded_templates_known_; }
    bool is_template_seeded(const std::string& name) const;
    void mark_template_seeded(const std::string& name);

    void set_roots(std::vector<Root> roots) { roots_ = std::move(roots); }
    void set_favorites(std::vector<std::string> f) { favorites_ = std::move(f); }
    void set_hide_front_matter(bool hide) { hide_front_matter_ = hide; }

    // Push `path` to the front of the recents list, de-duplicated, capped at
    // the same limit the Python app uses.
    void push_recent(const std::string& path);
    void clear_recents() { recents_.clear(); }

    bool is_favorite(const std::string& path) const;
    // Newest first, de-duplicated; adding past the cap drops the oldest.
    void add_favorite(const std::string& path);
    void remove_favorite(const std::string& path);
    void clear_favorites() { favorites_.clear(); }

    // This app's own window layout, kept apart from the Qt blobs.
    int window_width() const { return window_width_; }
    int window_height() const { return window_height_; }
    int editor_sash() const { return editor_sash_; }
    int files_sash() const { return files_sash_; }
    int outline_sash() const { return outline_sash_; }
    int recent_sash() const { return recent_sash_; }
    // Which columns are showing.  Hiding a pane is a deliberate choice and
    // should survive a restart, not be undone by it.
    bool show_files() const { return show_files_; }
    bool show_outline() const { return show_outline_; }
    bool show_editor() const { return show_editor_; }
    void set_show_files(bool v) { show_files_ = v; }
    void set_show_outline(bool v) { show_outline_ = v; }
    void set_show_editor(bool v) { show_editor_ = v; }
    int favorites_sash() const { return favorites_sash_; }
    void set_window_size(int width, int height);
    void set_editor_sash(int sash) { editor_sash_ = sash; }
    void set_files_sash(int sash) { files_sash_ = sash; }
    void set_outline_sash(int sash) { outline_sash_ = sash; }
    void set_recent_sash(int sash) { recent_sash_ = sash; }
    void set_favorites_sash(int sash) { favorites_sash_ = sash; }

private:
    std::vector<Root> roots_;
    std::vector<std::string> favorites_;
    std::vector<std::string> recents_;
    std::vector<std::string> flat_folders_;
    std::vector<std::string> excluded_folders_;
    std::string preview_theme_ = "github";
    std::vector<std::string> expanded_folders_;
    std::vector<std::string> seeded_templates_;
    bool seeded_templates_known_ = false;
    bool hide_front_matter_ = false;
    int window_width_ = 1280;
    int window_height_ = 820;
    int editor_sash_ = 520;
    int files_sash_ = 260;
    int outline_sash_ = 220;
    bool show_files_ = true;
    bool show_outline_ = true;
    bool show_editor_ = true;
    int recent_sash_ = 150;
    int favorites_sash_ = 170;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_CONFIG_H
