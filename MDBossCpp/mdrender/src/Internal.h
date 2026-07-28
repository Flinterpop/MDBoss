// Internal helpers shared between the mdrender translation units.
//
// Not installed and not part of the public API -- see include/mdrender for
// that.  Everything here is a free function over std::string so each piece
// stays unit-testable on its own.

#ifndef MDBOSS_MDRENDER_INTERNAL_H
#define MDBOSS_MDRENDER_INTERNAL_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mdrender/MdRender.h"

namespace mdrender::detail {

// Upper bound on lines we will scan in any one document.  Every loop in this
// library carries a bound (Rule of 10); a document larger than this is
// truncated rather than allowed to spin.
inline constexpr std::size_t kMaxLines = 2'000'000;

// HTML-escape as markdown-it's own escapeHtml() does: & < > " and *not* the
// apostrophe.  This is what ordinary text, code spans and attribute values go
// through in the Python renderer, because markdown-it emits them.
std::string escape_html(std::string_view text);

// HTML-escape as Python's html.escape(s, quote=True) does, apostrophe
// included (as &#x27;).  mdrender.py calls it directly in _render_fence, the
// two math renderers, _render_admonition and render_document, so those sites
// -- and only those -- escape apostrophes.
std::string escape_html_quoted(std::string_view text);

// Port of mdrender.py's _slug(): lowercase, drop punctuation, spaces to
// hyphens, de-duplicated through `seen` (which is mutated).
std::string slug(std::string_view text,
                 std::unordered_map<std::string, int>& seen);

// Split into lines, keeping no terminators.  Recognises CRLF and LF.
std::vector<std::string_view> split_lines(std::string_view text);

// One heading found by the source scan: level, raw source text (markup
// intact, exactly as Python's inline token content) and its slug.
struct ScannedHeading {
    int level = 1;
    std::string text;
    std::string slug;
};

// Fence-aware scan of the Markdown source for ATX (#) and setext (===/---)
// headings, in document order, assigning de-duplicated slugs.
//
// This works on the raw source rather than on parsed output because the slug
// is derived from the raw heading text: "## A [link](http://x)" slugs to
// "a-linkhttpx", not "a-link".  Deriving it from rendered text would diverge
// from the Python oracle.
std::vector<ScannedHeading> scan_headings(std::string_view md_text);

// Rewrite GitHub alerts (> [!NOTE]) and MkDocs admonitions (!!! / ??? / ???+)
// into raw HTML block wrappers, so a CommonMark parser with no rule-injection
// hook still produces the markup the Python renderer's custom markdown-it
// rules produce.  A blank line after the opening tag lets CommonMark end the
// HTML block and parse the body as Markdown.
std::string preprocess(std::string_view md_text);

// Strip active content from rendered HTML: <script>/<iframe> blocks, inline
// on* handler attributes and javascript: URLs.  Port of _sanitize_html.
std::string sanitize_html(std::string_view body);

// Render preprocessed Markdown to HTML with md4c, consuming `slugs` in
// heading order to emit stable anchor ids.  Returns false if md_parse fails.
bool render_html(std::string_view md_text,
                 const std::vector<ScannedHeading>& headings,
                 std::string& out);

}  // namespace mdrender::detail

#endif  // MDBOSS_MDRENDER_INTERNAL_H
