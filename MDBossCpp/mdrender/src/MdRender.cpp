// Public API: the C++ counterpart of mdrender.py's module-level functions.

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Internal.h"

namespace mdrender {
namespace {

// The C++ port renders through its own template, not the Python app's.  Two
// differences force that split, and neither is a matter of taste:
//
//   * template.html's scroll bridge loads qrc:///qtwebchannel/qwebchannel.js,
//     a resource that only exists inside QtWebEngine.  Under WebView2 the
//     bridge has to be window.chrome.webview instead.
//   * the Python renderer highlights code server-side with Pygments; this one
//     emits plain <pre><code class="language-x"> and lets the bundled
//     highlight.js do it in the page.
//
// So there is no @@MDBOSS_PYG_CSS@@ here, and two highlight.js placeholders
// the Python template does not have.
constexpr std::string_view kTemplateName = "template-webview2.html";

constexpr std::string_view kPhBase = "@@MDBOSS_BASE_HREF@@";
constexpr std::string_view kPhGhCss = "@@MDBOSS_GH_CSS@@";
constexpr std::string_view kPhHljsCss = "@@MDBOSS_HLJS_CSS@@";
constexpr std::string_view kPhHljsJs = "@@MDBOSS_HLJS_JS@@";
constexpr std::string_view kPhMermaid = "@@MDBOSS_MERMAID_JS@@";
constexpr std::string_view kPhKatexCss = "@@MDBOSS_KATEX_CSS@@";
constexpr std::string_view kPhKatexJs = "@@MDBOSS_KATEX_JS@@";
constexpr std::string_view kPhTitle = "@@MDBOSS_TITLE@@";
constexpr std::string_view kPhBody = "@@MDBOSS_BODY@@";
// The class on <body>.  The theme sheet needs it to outrank the template's own
// inline <style>, which is emitted after the linked stylesheets and would
// otherwise win every tie on .markdown-body.
constexpr std::string_view kPhTheme = "@@MDBOSS_THEME@@";

std::string& asset_dir_storage()
{
    static std::string dir;
    return dir;
}

// Default: an "assets" folder beside the running executable.
std::string default_asset_dir()
{
    std::error_code ec;
    const std::filesystem::path exe =
        std::filesystem::current_path(ec) / "assets";
    if (ec) {
        return "assets";
    }
    return exe.string();
}

std::string replace_all(std::string text, std::string_view needle,
                        std::string_view value)
{
    if (needle.empty()) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    std::size_t bound = 0;
    while (pos <= text.size()) {
        ++bound;
        assert(bound <= text.size() + 2 && "replace loop is bounded");
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

bool is_uri_unreserved(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' ||
           ch == '~' || ch == '/' || ch == ':';
}

// A file:/// URL for an absolute path, matching pathlib.Path.as_uri().
std::string path_to_uri(const std::filesystem::path& path)
{
    std::string text = path.generic_string();
    std::string encoded;
    encoded.reserve(text.size() + 16);
    for (const char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        if (is_uri_unreserved(ch)) {
            encoded += raw;
            continue;
        }
        constexpr char kHex[] = "0123456789ABCDEF";
        encoded += '%';
        encoded += kHex[(ch >> 4) & 0x0F];
        encoded += kHex[ch & 0x0F];
    }
    if (!encoded.empty() && encoded.front() == '/') {
        return "file://" + encoded;
    }
    return "file:///" + encoded;
}

std::string asset_uri(std::string_view name)
{
    const std::filesystem::path path =
        std::filesystem::path(asset_dir()) / std::filesystem::path(name);
    return path_to_uri(path);
}

// Body HTML and outline from one pass, so the ids and the outline agree.
void build(std::string_view md_text, bool strip_yaml, std::string* body,
           std::vector<Heading>* outline)
{
    const std::string source =
        strip_yaml ? strip_front_matter(md_text) : std::string(md_text);
    const std::string prepared = detail::preprocess(source);
    const std::vector<detail::ScannedHeading> headings =
        detail::scan_headings(prepared);

    if (outline != nullptr) {
        outline->clear();
        outline->reserve(headings.size());
        for (const detail::ScannedHeading& heading : headings) {
            outline->push_back(Heading{heading.level, heading.text,
                                       heading.slug});
        }
    }
    if (body == nullptr) {
        return;
    }
    std::string html;
    if (!detail::render_html(prepared, headings, html)) {
        body->clear();
        return;
    }
    *body = detail::sanitize_html(html);
}

}  // namespace

bool set_asset_dir(std::string_view dir)
{
    std::error_code ec;
    const std::filesystem::path path{std::string(dir)};
    if (!std::filesystem::is_directory(path, ec) || ec) {
        return false;
    }
    asset_dir_storage() = path.string();
    return true;
}

std::string asset_dir()
{
    if (asset_dir_storage().empty()) {
        asset_dir_storage() = default_asset_dir();
    }
    return asset_dir_storage();
}

std::vector<Heading> extract_outline(std::string_view md_text,
                                     bool strip_yaml)
{
    std::vector<Heading> outline;
    build(md_text, strip_yaml, nullptr, &outline);
    return outline;
}

std::string render_body(std::string_view md_text, bool strip_yaml)
{
    std::string body;
    build(md_text, strip_yaml, &body, nullptr);
    return body;
}

namespace {

// Trim ASCII whitespace, plus the quotes a YAML scalar may be wrapped in.
std::string tidy_title(std::string text)
{
    const char* const space = " \t\r\n";
    const std::size_t first = text.find_first_not_of(space);
    if (first == std::string::npos) {
        return {};
    }
    text = text.substr(first, text.find_last_not_of(space) - first + 1);
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
        text.back() == text.front()) {
        text = text.substr(1, text.size() - 2);
        const std::size_t inner = text.find_first_not_of(space);
        if (inner == std::string::npos) {
            return {};
        }
        text = text.substr(inner, text.find_last_not_of(space) - inner + 1);
    }
    return text;
}

// The value of a top-level `title:` key in a leading YAML block, if there is
// one.  Deliberately shallow: only a `---` block at the very start, only a key
// at column zero, so an indented `title:` nested under something else is not
// mistaken for the document's own.
std::string front_matter_title(std::string_view md_text)
{
    if (md_text.compare(0, 4, "---\n") != 0 &&
        md_text.compare(0, 5, "---\r\n") != 0) {
        return {};
    }
    std::size_t pos = md_text.find('\n') + 1;
    // Bounded by the text, and by a line count no front matter should reach.
    for (int line = 0; line < 200 && pos < md_text.size(); ++line) {
        std::size_t end = md_text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = md_text.size();
        }
        std::string_view row = md_text.substr(pos, end - pos);
        if (!row.empty() && row.back() == '\r') {
            row.remove_suffix(1);
        }
        if (row == "---" || row == "...") {
            return {};   // block closed without a title
        }
        if (row.compare(0, 6, "title:") == 0) {
            return tidy_title(std::string(row.substr(6)));
        }
        pos = end + 1;
    }
    return {};
}

}  // namespace

std::string document_title(std::string_view md_text)
{
    const std::string front = front_matter_title(md_text);
    if (!front.empty()) {
        return front;
    }
    // strip_yaml, so a front-matter line is never read as a heading.
    const std::vector<Heading> outline = extract_outline(md_text, true);
    if (outline.empty()) {
        return {};
    }
    return tidy_title(outline.front().text);
}

std::string theme_name(Theme theme)
{
    return theme == Theme::kNotes ? "notes" : "github";
}

Theme theme_from_name(std::string_view name)
{
    // Unknown values fall back rather than fail: this comes from a settings
    // file, and a profile written by a later build must not break an earlier
    // one.
    return name == "notes" ? Theme::kNotes : Theme::kGitHub;
}

std::string render_document(std::string_view md_text,
                            std::string_view base_href, std::string_view title,
                            bool strip_yaml, Theme theme)
{
    assert(!base_href.empty() && base_href.back() == '/' &&
           "base_href needs a trailing slash");

    const std::filesystem::path template_path =
        std::filesystem::path(asset_dir()) / kTemplateName;
    std::ifstream stream(template_path, std::ios::binary);
    if (!stream) {
        return {};   // caller sees an empty page rather than a partial one
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string page = buffer.str();

    // The only two things a theme changes, plus the body class.  Everything
    // else about the page -- and all of render_body() -- is identical.
    const bool notes = (theme == Theme::kNotes);
    const char* const body_css =
        notes ? "notes-light.css" : "github-markdown-light.css";
    const char* const code_css =
        notes ? "highlight/xcode.min.css" : "highlight/github.min.css";

    const std::pair<std::string_view, std::string> values[] = {
        {kPhBase, std::string(base_href)},
        {kPhGhCss, asset_uri(body_css)},
        {kPhHljsCss, asset_uri(code_css)},
        {kPhHljsJs, asset_uri("highlight/highlight.min.js")},
        {kPhMermaid, asset_uri("mermaid.min.js")},
        {kPhKatexCss, asset_uri("katex/katex.min.css")},
        {kPhKatexJs, asset_uri("katex/katex.min.js")},
    };
    for (const auto& [placeholder, value] : values) {
        page = replace_all(std::move(page), placeholder,
                           detail::escape_html_quoted(value));
    }
    page = replace_all(std::move(page), kPhTheme,
                       notes ? "theme-notes" : "theme-github");
    page = replace_all(std::move(page), kPhTitle,
                       detail::escape_html_quoted(title));
    page = replace_all(std::move(page), kPhBody,
                       render_body(md_text, strip_yaml));
    assert(page.find(kPhBody) == std::string::npos &&
           "body placeholder was substituted");
    return page;
}

}  // namespace mdrender
