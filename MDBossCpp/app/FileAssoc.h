// Windows file-type registration for MD Boss.
//
// Per-user (HKEY_CURRENT_USER), matching the per-user installer, so no admin
// rights are needed.  Note what this can and cannot do: since Windows 8 the
// UserChoice key is hash-protected, so no application can make itself the
// default handler.  These entries put MD Boss in the "Open with" list and in
// Settings > Default apps; choosing it there is the user's step.
//
// The layout is a pure plan so it can be tested without touching the registry,
// and so register and unregister cannot drift apart.  A golden test compares
// it against the plan the Python app builds from the same inputs.

#ifndef MDBOSS_APP_FILE_ASSOC_H
#define MDBOSS_APP_FILE_ASSOC_H

#include <string>
#include <vector>

namespace mdboss {

struct RegValue {
    std::string key;
    std::string name;
    std::string data;
};

struct RegSharedValue {
    std::string key;
    std::string name;
};

struct RegPlan {
    std::vector<RegValue> values;
    // Keys we create outright, deepest first so deletion never hits a key
    // that still has children.
    std::vector<std::string> owned_keys;
    // Keys shared with other applications: give up only our own value.
    std::vector<RegSharedValue> shared_values;
};

// `command` is the shell open command including its "%1" placeholder.
RegPlan registration_plan(const std::string& command, const std::string& icon,
                          const std::string& exe_name);

// The plan filled in for the running executable.
RegPlan current_registration_plan();

// The shell open command for this build, with the "%1" placeholder.
std::string handler_command();

bool apply_registration(const RegPlan& plan);
void remove_registration(const RegPlan& plan);

// True when our ProgID's open command is exactly `command`.  A mismatch means
// a stale registration -- the app moved, or another copy owns the ProgID.
bool is_registered(const std::string& command);

// Tell Explorer the association table changed, so it updates now.
void notify_assoc_changed();

}  // namespace mdboss

#endif  // MDBOSS_APP_FILE_ASSOC_H
