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

#include <string>
#include <vector>

namespace mdboss {

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
    bool hide_front_matter_ = false;
    int window_width_ = 1280;
    int window_height_ = 820;
    int editor_sash_ = 520;
    int files_sash_ = 260;
    int outline_sash_ = 220;
    int recent_sash_ = 150;
    int favorites_sash_ = 170;
};

}  // namespace mdboss

#endif  // MDBOSS_APP_CONFIG_H
