#include "Templates.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "Config.h"
#include "FileScan.h"
#include "LogoAsset.h"
#include "PathUtf8.h"

namespace mdboss {
namespace {

namespace fs = std::filesystem;

// Bounded (Rule of 10).
constexpr std::size_t kMaxTemplates = 1000;

struct Starter {
    std::string name;
    std::string body;
    // True for the two starters that shipped before templates were tracked by
    // name.  On a profile whose folder already exists these are recorded as
    // seeded without being written, so a user who deleted one does not get it
    // back the first time a *new* starter ships.
    bool legacy = false;
};

// The tech-note starter, in the house header form: the bare title as the first
// front-matter line (the Typora-exported shape), the banner line, the byline,
// then References first.  The logo is inline here; see Templates.h for why,
// and localize_embedded_logo() for what replaces it.
std::string technote_body()
{
    return "---\n"
           "{{title}}\n"
           "author: B. Graham\n"
           "version: 1.0\n"
           "creator: \n"
           "subject: Overview\n"
           "keywords: \n"
           "---\n"
           "<img src=\"" +
           logo_data_uri() +
           "\" alt=\"image-20240901145033347\" style=\"zoom: 50%;\" />"
           " TN {{year}}-0X\n"
           "\n"
           "B. Graham\n"
           "\n"
           "# {{title}}\n"
           "\n"
           "## References\n"
           "\n";
}

// Written once per name, and never over a file the user already has.
//
// Same text as app.py's _STARTER_TEMPLATES for the two it also has, but not
// the same bytes: Python opens these in text mode, so it writes CRLF, while
// this writes the LF it has.  Markdown does not care.  TechNote is this app's
// own -- the Python app is deprecated and gains nothing new.
std::vector<Starter> starters()
{
    std::vector<Starter> out;
    out.push_back(Starter{"Meeting Notes",
                          "# {{title}}\n\n"
                          "- **Date:** {{date}}\n"
                          "- **Attendees:** \n\n"
                          "## Agenda\n\n"
                          "## Notes\n\n"
                          "## Action items\n\n"
                          "- [ ] \n",
                          true});
    out.push_back(Starter{"Document",
                          "---\n"
                          "title: {{title}}\n"
                          "date: {{date}}\n"
                          "---\n\n"
                          "# {{title}}\n\n",
                          true});
    out.push_back(Starter{"TechNote", technote_body(), false});
    return out;
}

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

// Standard base64, no whitespace and no line breaks -- the input is a
// generated literal, so anything else means LogoAsset.h was edited by hand.
// Returns false rather than guessing, and the caller then does nothing.
bool base64_decode(std::string_view text, std::vector<unsigned char>& out)
{
    out.clear();
    if (text.empty() || text.size() % 4 != 0) {
        return false;
    }
    out.reserve(text.size() / 4 * 3);

    unsigned int accumulator = 0;
    int bits = 0;
    std::size_t padding = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {   // bounded by the literal
        const char ch = text[i];
        int value = -1;
        if (ch >= 'A' && ch <= 'Z') {
            value = ch - 'A';
        } else if (ch >= 'a' && ch <= 'z') {
            value = ch - 'a' + 26;
        } else if (ch >= '0' && ch <= '9') {
            value = ch - '0' + 52;
        } else if (ch == '+') {
            value = 62;
        } else if (ch == '/') {
            value = 63;
        } else if (ch == '=') {
            // Padding is legal only in the final quad, at most twice.
            if (i + 2 < text.size() || ++padding > 2) {
                return false;
            }
            continue;
        } else {
            return false;
        }
        if (padding != 0) {
            return false;   // a symbol after the padding started
        }
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(
                static_cast<unsigned char>((accumulator >> bits) & 0xFFu));
        }
    }
    return true;
}

}  // namespace

std::string templates_dir()
{
    return path_to_utf8(path_from_utf8(user_data_dir()) / "templates");
}

bool seed_templates(Config& config)
{
    const fs::path dir = path_from_utf8(templates_dir());
    std::error_code ec;
    const bool existed = fs::exists(dir, ec);
    if (!existed && (!fs::create_directories(dir, ec) || ec)) {
        return false;   // templates are a convenience, never load bearing
    }

    const std::vector<Starter> all = starters();
    assert(!all.empty() && "there is always at least one starter");

    bool changed = false;
    // A folder that predates the per-name record was seeded by an older build,
    // so adopt its starters as already offered rather than writing them again.
    if (existed && !config.knows_seeded_templates()) {
        for (const Starter& starter : all) {   // bounded: a fixed list
            if (starter.legacy) {
                config.mark_template_seeded(starter.name);
                changed = true;
            }
        }
    }

    for (const Starter& starter : all) {   // bounded: a fixed list
        if (config.is_template_seeded(starter.name)) {
            continue;
        }
        config.mark_template_seeded(starter.name);
        changed = true;
        const fs::path file = dir / (starter.name + ".md");
        if (fs::exists(file, ec)) {
            continue;   // the user's own file of that name wins
        }
        std::ofstream stream(file, std::ios::binary);
        if (stream) {
            stream << starter.body;
        }
    }
    return changed;
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
    // {{year}} exists for the tech-note number, TN <year>-0X, which would
    // otherwise be a hard-coded year going stale in the template folder.
    out = replace_all(out, "{{year}}", formatted(when, "%Y"));
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

std::string logo_data_uri()
{
    return std::string("data:image/png;base64,") + kLogoPngBase64;
}

std::vector<unsigned char> logo_png_bytes()
{
    std::vector<unsigned char> bytes;
    if (!base64_decode(kLogoPngBase64, bytes)) {
        return {};
    }
    // The generated header records the length it encoded, so a truncated or
    // hand-edited blob is caught here rather than written out as a corrupt
    // .png that every note then points at.
    if (bytes.size() != kLogoPngBytes) {
        return {};
    }
    return bytes;
}

bool has_embedded_logo(const std::string& text)
{
    return text.find(logo_data_uri()) != std::string::npos;
}

std::string localize_embedded_logo(const std::string& text,
                                   const std::string& document_path)
{
    assert(!document_path.empty() && "the document needs a path to sit beside");
    if (!has_embedded_logo(text)) {
        return text;
    }

    const fs::path dir = path_from_utf8(document_path).parent_path();
    if (dir.empty()) {
        return text;
    }
    const fs::path target = dir / kLogoFileName;

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        const std::vector<unsigned char> bytes = logo_png_bytes();
        if (bytes.empty()) {
            return text;   // damaged blob: leave the document rendering
        }
        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return text;   // read-only folder, say: keep the inline copy
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream.good()) {
            return text;
        }
        stream.close();
        // Written short is the one failure that would leave a broken .png on
        // disk *and* a document pointing at it, so check before swapping.
        if (fs::file_size(target, ec) != bytes.size() || ec) {
            return text;
        }
    }

    return replace_all(text, logo_data_uri(), kLogoFileName);
}

}  // namespace mdboss
