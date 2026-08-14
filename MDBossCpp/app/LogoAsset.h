// The tech-note banner logo, embedded so a new document renders it before it
// has a folder to resolve a relative <img src> against.
//
// GENERATED FILE -- do not edit by hand.  Produced from background-logo.png
// (4856 bytes, SHA-256 691c495c15e3da87fa4d50928c652ba62cc984303cedd66974e1464f50a10523) by MDBossCpp/tools/embed_logo.py.
//
// This is the single source of truth for the logo: Templates.cpp decodes it
// both to build the data: URI the template carries and to write the .png file
// beside a saved document.

#ifndef MDBOSS_APP_LOGO_ASSET_H
#define MDBOSS_APP_LOGO_ASSET_H

#include <cstddef>

namespace mdboss {

// Decoded size, so the base64 round trip can be checked rather than trusted.
inline constexpr std::size_t kLogoPngBytes = 4856;

// Base64 of background-logo.png, one string once the adjacent literals are
// concatenated.  ASCII only, so the narrow-literal guard in test_sources.cpp
// is satisfied.
inline constexpr char kLogoPngBase64[] =
    "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";

}  // namespace mdboss

#endif  // MDBOSS_APP_LOGO_ASSET_H
