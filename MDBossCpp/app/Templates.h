// New-document templates: Markdown files in %APPDATA%\MDBoss\templates,
// shared with the Python app, with a few placeholders substituted on use.

#ifndef MDBOSS_APP_TEMPLATES_H
#define MDBOSS_APP_TEMPLATES_H

#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace mdboss {

class Config;

std::string templates_dir();

// Write any starter template the user has not been offered yet, recording
// each one in `config` so it is offered exactly once.  Returns true if
// `config` changed and the caller should save it.
//
// Per-name rather than per-folder, because a starter added in a later version
// has to reach a profile whose templates folder already exists.  Deleting a
// template still means it: a name is marked as seeded whether or not the file
// was actually written, so it never comes back.  An existing file of the same
// name is never overwritten.
bool seed_templates(Config& config);

// (name, path) for each Markdown template, sorted by name.
std::vector<std::pair<std::string, std::string>> list_templates();

// Substitute {{title}}, {{date}}, {{time}}, {{datetime}} and {{year}}.
//
// `when` is passed in rather than read from the clock so the substitution is
// deterministic under test; the caller supplies the current local time.
std::string apply_template(const std::string& text, const std::string& title,
                           const std::tm& when);

// apply_template() with the current local time.
std::string apply_template(const std::string& text, const std::string& title);

// The TechNote starter's raw text, placeholders unsubstituted.
//
// Exposed for the test that pins the house front matter down.  It is a string
// literal nothing else checks, and the header is exactly the thing people
// notice when it is wrong.
std::string technote_template();

// ---- The tech-note banner logo -------------------------------------------
//
// The TechNote starter carries the logo inline, as a data: URI, because a
// document created from it has no folder yet: a relative <img src> would have
// nothing to resolve against and the banner would render as a broken image
// until the file was saved *and* the .png copied beside it.  A data: URI needs
// neither, and the preview's network lock already permits that scheme.
//
// The inline form is a scaffold, not the finished shape.  As soon as the
// document has a folder, localize_embedded_logo() writes the .png beside it
// and swaps the URI for the plain relative reference the tech-note convention
// calls for -- so a finished note looks exactly like every hand-written one.

// The file the swap writes and points at.
inline constexpr char kLogoFileName[] = "background-logo.png";

// The logo as a complete data: URI, ready to be an <img src> value.
std::string logo_data_uri();

// The logo's bytes, decoded from the generated blob in LogoAsset.h.  Empty if
// that blob has been damaged, which the caller must treat as "do nothing".
std::vector<unsigned char> logo_png_bytes();

// True if `text` carries the exact logo this app embeds.  A data: URI the
// user wrote themselves is not a match and is left alone.
bool has_embedded_logo(const std::string& text);

// Write kLogoFileName into `document_path`'s folder and return `text` with the
// embedded data: URI replaced by a relative reference to it.
//
// Returns `text` unchanged when it carries no embedded logo, and when the .png
// cannot be written -- a document that still renders beats one whose banner is
// a broken-image box.  An existing background-logo.png is never overwritten:
// a folder that already has one has the copy its other notes point at.
std::string localize_embedded_logo(const std::string& text,
                                   const std::string& document_path);

}  // namespace mdboss

#endif  // MDBOSS_APP_TEMPLATES_H
