#include "Templates.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>

#include "Config.h"
#include "FileScan.h"
#include "PathUtf8.h"

namespace mdboss {
namespace {

namespace fs = std::filesystem;

// Bounded (Rule of 10).
constexpr std::size_t kMaxTemplates = 1000;

// Written on first run only, and only when the folder does not already exist,
// so whichever build runs first wins and the other leaves them alone.
//
// Same text as app.py's _STARTER_TEMPLATES, but not the same bytes: Python
// opens these in text mode, so it writes CRLF, while this writes the LF it
// has.  Markdown does not care, and the seeding happens at most once.
const std::pair<const char*, const char*> kStarters[] = {
    {"Meeting Notes",
     "# {{title}}\n\n"
     "- **Date:** {{date}}\n"
     "- **Attendees:** \n\n"
     "## Agenda\n\n"
     "## Notes\n\n"
     "## Action items\n\n"
     "- [ ] \n"},
    {"Document",
     "---\n"
     "title: {{title}}\n"
     "date: {{date}}\n"
     "---\n\n"
     "# {{title}}\n\n"},
};

std::string replace_all(std::string text, const std::string& needle,
                        const std::string& value)
{
    if (needle.empty()) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t hit = text.find(needle, pos);
        if (hit == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        out.append(text, pos, hit - pos);
        out.append(value);
        pos = hit + needle.size();
    }
    return out;
}

std::string formatted(const std::tm& when, const char* format)
{
    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), format, &when) == 0) {
        return {};
    }
    return buffer;
}

std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    });
    return text;
}

}  // namespace

std::string templates_dir()
{
    return path_to_utf8(path_from_utf8(user_data_dir()) / "templates");
}

void seed_templates()
{
    const fs::path dir = path_from_utf8(templates_dir());
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        return;   // already set up, or deliberately emptied
    }
    if (!fs::create_directories(dir, ec) || ec) {
        return;   // templates are a convenience, never load bearing
    }
    for (const auto& [name, body] : kStarters) {
        std::ofstream stream(dir / (std::string(name) + ".md"),
                             std::ios::binary);
        if (stream) {
            stream << body;
        }
    }
}

std::vector<std::pair<std::string, std::string>> list_templates()
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const Entry& entry : list_directory(templates_dir())) {
        if (out.size() >= kMaxTemplates) {
            break;
        }
        if (entry.is_dir) {
            continue;
        }
        // list_directory already filters to Markdown; strip the extension for
        // the display name, as the Python app does.
        const std::string stem =
            path_to_utf8(path_from_utf8(entry.name).stem());
        out.emplace_back(stem, entry.path);
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  return lowered(a.first) < lowered(b.first);
              });
    return out;
}

std::string apply_template(const std::string& text, const std::string& title,
                           const std::tm& when)
{
    std::string out = replace_all(text, "{{title}}", title);
    out = replace_all(out, "{{date}}", formatted(when, "%Y-%m-%d"));
    out = replace_all(out, "{{time}}", formatted(when, "%H:%M"));
    // Order is safe: no placeholder is a substring of another, because the
    // closing braces break the overlap ({{datetime}} contains neither
    // "{{date}}" nor "{{time}}").  A test pins that down so a future
    // placeholder cannot quietly break it.
    out = replace_all(out, "{{datetime}}", formatted(when, "%Y-%m-%d %H:%M"));
    return out;
}

std::string apply_template(const std::string& text, const std::string& title)
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0) {
        return replace_all(text, "{{title}}", title);
    }
    return apply_template(text, title, local);
}

}  // namespace mdboss
