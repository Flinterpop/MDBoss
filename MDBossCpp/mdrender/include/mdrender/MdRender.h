// Pure Markdown -> HTML rendering for MD Boss (no GUI dependency).
//
// This is the C++ port of the repo-root ``mdrender.py``.  The Python module is
// the reference implementation and the parity oracle: where the two disagree,
// this port is wrong.  The public API is deliberately identical in shape --
// extract_outline / render_body / render_document / strip_front_matter -- so
// the golden-corpus harness in tests/ can compare them one to one.
//
// Nothing here reaches the network.  Image ``src`` and link ``href`` values are
// left untouched; the page's ``<base href>`` (set by render_document) resolves
// relative paths against the document's own folder on disk.

#ifndef MDBOSS_MDRENDER_H
#define MDBOSS_MDRENDER_H

#include <string>
#include <string_view>
#include <vector>

namespace mdrender {

// One entry of the document outline: heading level 1..6, its display text, and
// the GitHub-like slug used as the anchor id in the rendered HTML.
struct Heading {
    int level = 1;
    std::string text;
    std::string slug;
};

// Remove a leading YAML (---) or TOML (+++) front-matter block, if present.
// Only a block anchored at the very start is removed, so a horizontal rule
// further down the document is untouched.
std::string strip_front_matter(std::string_view md_text);

// The heading outline, in document order, with de-duplicated slugs.
std::vector<Heading> extract_outline(std::string_view md_text,
                                     bool strip_yaml = false);

// What the document calls itself, or empty when it says nothing.
//
// A YAML front-matter ``title:`` wins, being the one place a document states
// its title outright; otherwise the first heading, whatever its level.  The
// heading comes from extract_outline(), so a ``#`` inside a fenced code block
// is not mistaken for one.
//
// This is not a Python-parity function: the deprecated app has no equivalent.
std::string document_title(std::string_view md_text);

// Just the ``.markdown-body`` inner HTML -- no page chrome.
std::string render_body(std::string_view md_text, bool strip_yaml = false);

// A complete HTML page for md_text.  base_href must be a file:/// URL to the
// document's own folder, with a trailing slash, so relative images resolve.
std::string render_document(std::string_view md_text,
                            std::string_view base_href,
                            std::string_view title = "MDBoss",
                            bool strip_yaml = false);

// Directory holding the bundled render assets (github-markdown-light.css,
// mermaid.min.js, katex/, template.html).  Defaults to "assets" beside the
// running executable; tests and the app override it.  Returns false if the
// path does not name an existing directory, leaving the previous value.
bool set_asset_dir(std::string_view dir);
std::string asset_dir();

}  // namespace mdrender

#endif  // MDBOSS_MDRENDER_H
