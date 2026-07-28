// New-document templates: Markdown files in %APPDATA%\MDBoss\templates,
// shared with the Python app, with a few placeholders substituted on use.

#ifndef MDBOSS_APP_TEMPLATES_H
#define MDBOSS_APP_TEMPLATES_H

#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace mdboss {

std::string templates_dir();

// On first run only, create the folder with a couple of starters.  Does
// nothing if the folder already exists -- a user who deleted every template
// meant it.
void seed_templates();

// (name, path) for each Markdown template, sorted by name.
std::vector<std::pair<std::string, std::string>> list_templates();

// Substitute {{title}}, {{date}}, {{time}} and {{datetime}}.
//
// `when` is passed in rather than read from the clock so the substitution is
// deterministic under test; the caller supplies the current local time.
std::string apply_template(const std::string& text, const std::string& title,
                           const std::tm& when);

// apply_template() with the current local time.
std::string apply_template(const std::string& text, const std::string& title);

}  // namespace mdboss

#endif  // MDBOSS_APP_TEMPLATES_H
