// Checking GitHub for a newer release.
//
// This is the ONLY outbound request MD Boss makes, and it carries no document
// data: a GET to the public releases API, and then a download of a release
// asset.  The preview's network lock is unaffected -- documents still cannot
// reach the network; this is the application asking about itself.
//
// The parsing is separated from the fetching so the interesting half can be
// tested without a network: version comparison and picking the right asset out
// of a release payload are where the bugs live, not in the HTTP.

#ifndef MDBOSS_APP_UPDATER_H
#define MDBOSS_APP_UPDATER_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mdboss {

struct ReleaseInfo {
    std::vector<int> version;     // empty when the tag was unusable
    std::string version_str;      // tag without a leading v
    std::string setup_url;        // installer asset, empty if absent
    std::string portable_url;     // portable zip asset, empty if absent
    std::string html_url;         // the release page, for the fallback
};

// "v1.2.3" / "1.2.3" -> {1,2,3}.  Nothing for a malformed tag.
std::optional<std::vector<int>> parse_version(const std::string& text);

// Component-wise, with a missing component treated as 0 so 1.2 < 1.2.1.
bool is_newer(const std::vector<int>& candidate,
              const std::vector<int>& current);

// Pull the fields we care about out of a GitHub release payload.  A body that
// is not a release object yields an empty version rather than throwing.
ReleaseInfo parse_release(const std::string& json_body);

// Ask GitHub for the newest release.  `done` runs on the UI thread with the
// parsed release, or with an empty version and a message on failure.
void check_for_update(
    std::function<void(const ReleaseInfo&, const std::string& error)> done);

// The assets this build would install.  Distinct from the Python build's
// asset names, so one release can carry both without either app grabbing the
// other's build.
extern const char* const kSetupAssetName;
extern const char* const kPortableAssetName;

// True when this looks like a loose portable copy: no Inno uninstaller
// (unins*.exe) beside the exe.  Mirrors running_portable() in app.py,
// including its lean: a directory that cannot be listed is treated as
// portable, which at worst downloads a zip instead of an installer.
bool portable_install(const std::string& exe_dir_utf8);

// The batch that installs an update after we have exited, then relaunches.
//
// It cannot run while we are running: the installer has to overwrite an exe
// Windows holds a lock on for as long as the process lives, and a fixed sleep
// is not enough because shutdown can outlast it.  So this waits for OUR
// PROCESS to disappear before touching anything.
//
// By pid, not by image name, which is where this differs from the Python
// app's otherwise identical batch: both builds install an exe called
// MDBoss.exe, so waiting on the name would also wait on a running Python MD
// Boss -- a full minute of nothing, for an unrelated program.
//
// Every line runs whether or not the one before it worked, so a failed
// install still relaunches the intact old version.
//
// The install line carries install_scope_flag(app_exe): the installer defaults
// to per-machine and a /VERYSILENT run takes the default, so a per-user copy
// updating without /CURRENTUSER would elevate and plant a second install in
// Program Files instead of updating itself.
std::string installer_batch(const std::string& setup_path,
                            const std::string& app_exe, unsigned long pid);

// "/ALLUSERS" when app_exe lives under Program Files, "/CURRENTUSER"
// otherwise.  Mirrors _install_scope_flag in app.py.
std::string install_scope_flag(const std::string& app_exe);

// The batch that swaps a portable copy for a downloaded update zip.
//
// DELIBERATE divergence from app.py, which unpacks the zip in-process before
// closing: this app has no zip library, so the batch extracts with the
// bsdtar Windows ships as %SystemRoot%\System32\tar.exe (absolute path, like
// every other tool here) into `staging_dir` and only robocopies if it finds
// MDBoss.exe there -- at the zip root or one folder down, the two layouts
// app.py accepts.  Same no-brick property either way: robocopy copies OVER
// the install rather than replacing it, the relaunch runs whatever happened,
// and a junk zip updates nothing.
std::string portable_batch(const std::string& zip_path,
                           const std::string& staging_dir,
                           const std::string& app_exe, unsigned long pid);

// Download `url` to `dest`.  `done` runs on the UI thread; `error` is empty
// on success.  Nothing else in the app downloads anything.
void download_update(const std::string& url, const std::string& dest,
                     std::function<void(const std::string& error)> done);

}  // namespace mdboss

#endif  // MDBOSS_APP_UPDATER_H
