// Small text utilities: HTML escaping, slugging and line splitting.

#include <cassert>
#include <cstddef>

#include "Internal.h"

namespace mdrender::detail {
namespace {

// True for the ASCII characters Python's str.lower() would leave alone and
// _SLUG_STRIP (`[^\w\- ]+`) would keep: word characters, hyphen and space.
//
// Bytes >= 0x80 are kept as-is.  Python's `\w` is Unicode-aware, so an accented
// letter survives slugging there; treating every UTF-8 continuation byte as a
// word character reproduces that without pulling in a Unicode table.  The one
// residual difference is case: Python lowercases non-ASCII letters and this
// does not, so a heading of "CAFÉ" slugs to "cafÉ" here and "café" there.
// Documented rather than fixed -- ASCII headings, which is all the corpus and
// both shipped documents contain, are byte-identical.
bool is_slug_keep(unsigned char ch)
{
    const bool ascii_word = (ch >= 'a' && ch <= 'z') ||
                            (ch >= 'A' && ch <= 'Z') ||
                            (ch >= '0' && ch <= '9') || ch == '_';
    return ascii_word || ch == '-' || ch == ' ' || ch >= 0x80;
}

char ascii_lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
               ? static_cast<char>(ch - 'A' + 'a')
               : ch;
}

}  // namespace

namespace {

std::string escape_impl(std::string_view text, bool escape_apostrophe)
{
    std::string out;
    out.reserve(text.size());
    std::size_t bound = 0;
    for (const char ch : text) {
        ++bound;
        assert(bound <= text.size() && "escape loop is bounded");
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'':
            out += escape_apostrophe ? "&#x27;" : "'";
            break;
        default: out += ch; break;
        }
    }
    return out;
}

}  // namespace

std::string escape_html(std::string_view text)
{
    return escape_impl(text, false);
}

std::string escape_html_quoted(std::string_view text)
{
    return escape_impl(text, true);
}

std::string slug(std::string_view text,
                 std::unordered_map<std::string, int>& seen)
{
    // Python strips the text first, then removes disallowed characters.
    std::size_t begin = 0;
    std::size_t end = text.size();
    std::size_t bound = 0;
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
        ++bound;
        assert(bound <= text.size() && "leading trim is bounded");
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
        ++bound;
        assert(bound <= 2 * text.size() + 1 && "trailing trim is bounded");
    }

    std::string base;
    base.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        const char ch = text[i];
        if (!is_slug_keep(static_cast<unsigned char>(ch))) {
            continue;
        }
        base += (ch == ' ') ? '-' : ascii_lower(ch);
    }

    const int count = seen[base]++;
    if (count == 0) {
        return base;
    }
    return base + "-" + std::to_string(count);
}

std::vector<std::string_view> split_lines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    std::size_t bound = 0;
    while (start <= text.size()) {
        ++bound;
        if (bound > kMaxLines) {
            break;
        }
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        std::size_t end = nl;
        if (end > start && text[end - 1] == '\r') {
            --end;
        }
        lines.push_back(text.substr(start, end - start));
        start = nl + 1;
    }
    return lines;
}

}  // namespace mdrender::detail
