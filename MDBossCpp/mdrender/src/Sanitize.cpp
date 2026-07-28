// Defence-in-depth HTML sanitiser.
//
// Port of mdrender.py's _sanitize_html.  Raw HTML is deliberately enabled in
// the parser so Typora/GitHub style embeds render; this strips the active
// parts afterwards.  The preview web view is network-locked as well, so this
// is the second of two layers, not the only one.
//
// Written as hand-rolled scanners rather than std::regex: the Python patterns
// are case-insensitive and DOTALL over potentially large documents, and
// std::regex is both slow and stack-hungry on inputs that size.

#include <cassert>
#include <cstddef>

#include "Internal.h"

namespace mdrender::detail {
namespace {

char lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
               ? static_cast<char>(ch - 'A' + 'a')
               : ch;
}

bool is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == '\f' || ch == '\v';
}

// Case-insensitive compare of text[pos..] against `needle`.
bool match_ci(std::string_view text, std::size_t pos, std::string_view needle)
{
    if (pos + needle.size() > text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (lower(text[pos + i]) != lower(needle[i])) {
            return false;
        }
    }
    return true;
}

// Skip a run of whitespace starting at `pos`.
std::size_t skip_space(std::string_view text, std::size_t pos)
{
    while (pos < text.size() && is_space(text[pos])) {
        ++pos;
    }
    return pos;
}

// Matches `<\s*TAG\b`, returning the offset just past the tag name, or npos.
std::size_t match_open_tag(std::string_view text, std::size_t pos,
                           std::string_view tag)
{
    if (pos >= text.size() || text[pos] != '<') {
        return std::string_view::npos;
    }
    const std::size_t name = skip_space(text, pos + 1);
    if (!match_ci(text, name, tag)) {
        return std::string_view::npos;
    }
    const std::size_t after = name + tag.size();
    // \b: the tag name must not run on into a longer name.
    if (after < text.size()) {
        const char ch = lower(text[after]);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            return std::string_view::npos;
        }
    }
    return after;
}

// Matches `<\s*/\s*TAG\s*>`, returning the offset just past '>', or npos.
std::size_t match_close_tag(std::string_view text, std::size_t pos,
                            std::string_view tag)
{
    if (pos >= text.size() || text[pos] != '<') {
        return std::string_view::npos;
    }
    std::size_t at = skip_space(text, pos + 1);
    if (at >= text.size() || text[at] != '/') {
        return std::string_view::npos;
    }
    at = skip_space(text, at + 1);
    if (!match_ci(text, at, tag)) {
        return std::string_view::npos;
    }
    at = skip_space(text, at + tag.size());
    if (at >= text.size() || text[at] != '>') {
        return std::string_view::npos;
    }
    return at + 1;
}

// Remove every `<tag ...> ... </tag>` region, non-greedy like the Python
// pattern: the first closing tag ends the region.
std::string strip_element(std::string_view body, std::string_view tag)
{
    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    std::size_t bound = 0;
    while (i < body.size()) {
        ++bound;
        assert(bound <= body.size() + 1 && "strip loop is bounded");
        if (match_open_tag(body, i, tag) == std::string_view::npos) {
            out += body[i];
            ++i;
            continue;
        }
        // Find the matching close; if there is none the pattern does not
        // match at all, so the text is kept verbatim.
        std::size_t j = i + 1;
        std::size_t end = std::string_view::npos;
        while (j < body.size()) {
            const std::size_t close = match_close_tag(body, j, tag);
            if (close != std::string_view::npos) {
                end = close;
                break;
            }
            ++j;
        }
        if (end == std::string_view::npos) {
            out += body[i];
            ++i;
            continue;
        }
        i = end;
    }
    return out;
}

// Remove inline `on*="..."` event-handler attributes.
std::string strip_on_attributes(std::string_view body)
{
    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    std::size_t bound = 0;
    while (i < body.size()) {
        ++bound;
        assert(bound <= body.size() + 1 && "attribute loop is bounded");
        // The Python pattern requires a leading whitespace character.
        if (!is_space(body[i]) || !match_ci(body, i + 1, "on")) {
            out += body[i];
            ++i;
            continue;
        }
        std::size_t at = i + 3;   // past the whitespace and "on"
        while (at < body.size() &&
               ((lower(body[at]) >= 'a' && lower(body[at]) <= 'z'))) {
            ++at;
        }
        if (at == i + 3) {        // "on" with no handler name after it
            out += body[i];
            ++i;
            continue;
        }
        const std::size_t eq = skip_space(body, at);
        if (eq >= body.size() || body[eq] != '=') {
            out += body[i];
            ++i;
            continue;
        }
        std::size_t value = skip_space(body, eq + 1);
        if (value < body.size() && (body[value] == '"' || body[value] == '\'')) {
            const char quote = body[value];
            const std::size_t close = body.find(quote, value + 1);
            if (close == std::string_view::npos) {
                out += body[i];
                ++i;
                continue;
            }
            i = close + 1;
            continue;
        }
        while (value < body.size() && !is_space(body[value]) &&
               body[value] != '>') {
            ++value;
        }
        i = value;
    }
    return out;
}

// Replace `href="javascript:..."` / `src='javascript:...'` with `="#"`.
std::string strip_js_urls(std::string_view body)
{
    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    std::size_t bound = 0;
    while (i < body.size()) {
        ++bound;
        assert(bound <= body.size() + 1 && "url loop is bounded");
        std::string_view attr;
        if (match_ci(body, i, "href")) {
            attr = "href";
        } else if (match_ci(body, i, "src")) {
            attr = "src";
        }
        if (attr.empty()) {
            out += body[i];
            ++i;
            continue;
        }
        const std::size_t eq = skip_space(body, i + attr.size());
        if (eq >= body.size() || body[eq] != '=') {
            out += body[i];
            ++i;
            continue;
        }
        const std::size_t quote_at = eq + 1;
        if (quote_at >= body.size() ||
            (body[quote_at] != '"' && body[quote_at] != '\'')) {
            out += body[i];
            ++i;
            continue;
        }
        const char quote = body[quote_at];
        const std::size_t scheme = skip_space(body, quote_at + 1);
        if (!match_ci(body, scheme, "javascript:")) {
            out += body[i];
            ++i;
            continue;
        }
        // The Python pattern is [^"'>]* up to the matching quote.
        std::size_t end = scheme + 11;
        while (end < body.size() && body[end] != '"' && body[end] != '\'' &&
               body[end] != '>') {
            ++end;
        }
        if (end >= body.size() || body[end] != quote) {
            out += body[i];
            ++i;
            continue;
        }
        out += attr;
        out += "=\"#\"";
        i = end + 1;
    }
    return out;
}

}  // namespace

std::string sanitize_html(std::string_view body)
{
    std::string out = strip_element(body, "script");
    out = strip_element(out, "iframe");
    out = strip_on_attributes(out);
    out = strip_js_urls(out);
    return out;
}

}  // namespace mdrender::detail
