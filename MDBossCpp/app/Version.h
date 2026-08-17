// One definition of the product name and version.
//
// The repo versions its apps in lockstep -- app.py, installer.iss and this
// must agree -- so this constant is bumped with the others rather than on its
// own.  The stage string is separate: it says this build is the C++ port,
// which is not yet at parity with the shipping Python app, so an About box
// showing the shared version number is not mistaken for the shipped one.

#ifndef MDBOSS_APP_VERSION_H
#define MDBOSS_APP_VERSION_H

namespace mdboss {

inline constexpr const char* kAppName = "MD Boss";
inline constexpr const char* kAppVersion = "1.8.0";
inline constexpr const char* kAppStage = "C++ port (in development)";
inline constexpr const char* kAttribution = "Bungee Studios 2026  B.Graham";

}  // namespace mdboss

#endif  // MDBOSS_APP_VERSION_H
