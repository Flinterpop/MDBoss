// New-document templates: Markdown files in <user_data_dir()>\templates
// (%APPDATA%\MDBoss\templates for MD Boss, shared with the Python app), with a
// few placeholders substituted on use.

#ifndef MDBOSS_APP_TEMPLATES_H
#define MDBOSS_APP_TEMPLATES_H

#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace mdboss {

// <user_data_dir()>\templates.
std::string templates_dir();

// Which starters this profile has been offered.  A plain record rather than a
// Config&, so the templates unit does not drag the app's settings file (and
// with it, the app's profile) into anything that compiles it.  The caller
// loads it from its settings and writes back what seed_templates() adds.
struct SeededTemplates {
    // False until anything has been recorded: "offered nothing yet" is not the
    // same as "the record predates per-name seeding".
    bool known = false;
    std::vector<std::string> names;

    bool has(const std::string& name) const;
    void mark(const std::string& name);
};

// Write any starter template the user has not been offered yet, recording
// each one in `seeded` so it is offered exactly once.  Returns true if
// `seeded` changed and the caller should save it.
//
// Per-name rather than per-folder, because a starter added in a later version
// has to reach a profile whose templates folder already exists.  Deleting a
// template still means it: a name is marked as seeded whether or not the file
// was actually written, so it never comes back.  An existing file of the same
// name is never overwritten.
bool seed_templates(SeededTemplates& seeded);

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

// A fresh v4 UUID, the form {{guid}} is replaced with.
//
// Exposed because "update this document as a tech note" needs the same
// identifier the template gets, and two ways of minting one is one too many.
std::string new_guid();

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
