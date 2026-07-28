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

std::string render_document(std::string_view md_text,
                            std::string_view base_href, std::string_view title,
                            bool strip_yaml)
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

    const std::pair<std::string_view, std::string> values[] = {
        {kPhBase, std::string(base_href)},
        {kPhGhCss, asset_uri("github-markdown-light.css")},
        {kPhHljsCss, asset_uri("highlight/github.min.css")},
        {kPhHljsJs, asset_uri("highlight/highlight.min.js")},
        {kPhMermaid, asset_uri("mermaid.min.js")},
        {kPhKatexCss, asset_uri("katex/katex.min.css")},
        {kPhKatexJs, asset_uri("katex/katex.min.js")},
    };
    for (const auto& [placeholder, value] : values) {
        page = replace_all(std::move(page), placeholder,
                           detail::escape_html_quoted(value));
    }
    page = replace_all(std::move(page), kPhTitle,
                       detail::escape_html_quoted(title));
    page = replace_all(std::move(page), kPhBody,
                       render_body(md_text, strip_yaml));
    assert(page.find(kPhBody) == std::string::npos &&
           "body placeholder was substituted");
    return page;
}

}  // namespace mdrender
