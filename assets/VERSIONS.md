# Vendored assets

Everything the preview needs is bundled here, because the preview is
network-locked: a CDN reference would simply fail to load, silently, and the
page would render without maths or diagrams. Nothing in this folder may point
at a remote URL.

This file exists because the versions were otherwise unrecoverable. A minified
bundle rarely says what it is, and guessing from the first version-shaped
string in it is worse than not guessing — `mermaid.min.js` contains
`version:"3.4.0"`, which is the DOMPurify it bundles, not mermaid.

**Update this file in the same commit that replaces an asset.** A version
recorded here and not on disk is worse than no record at all.

| File | Version | Upstream | Licence |
|---|---|---|---|
| `highlight/highlight.min.js` | 11.12.0 | highlight.js | BSD-3-Clause |
| `highlight/github.min.css` | 11.12.0 | highlight.js, `styles/github.min.css` | BSD-3-Clause |
| `katex/katex.min.js` | 0.18.4 | KaTeX | MIT |
| `katex/katex.min.css` | 0.18.4 | KaTeX | MIT |
| `katex/fonts/*.woff2` (20 files) | 0.18.4 | KaTeX | MIT (fonts: OFL) |
| `mermaid.min.js` | 11.16.1 | mermaid | MIT |
| `github-markdown-light.css` | 5.9.0 | github-markdown-css | MIT |
| `pygments-github.css` | generated | Pygments `HtmlFormatter(style="default")` | BSD-2-Clause |

## How each version was established

Recorded from the files themselves, not from memory:

- **highlight.js** states it in its own banner: `Highlight.js v11.12.0`. The
  theme has no banner and is only meaningful paired with that release, so it
  carries the same number.
- **KaTeX** embeds `version:"0.18.4"` in the bundle.
- **mermaid** embeds `{version:"11.16.1"}` next to its own accessor. The other
  version string in that file belongs to DOMPurify; see above.

  Raised from 11.16.0 to 11.16.1 on 2026-08-14 by a rot check: the GitHub
  Advisory Database listed five advisories against 11.16.0, all first patched
  in 11.16.1 — radar-diagram DoS (GHSA-rhh3-jpg6-66xh), XY-chart infinite-loop
  DoS (GHSA-2v8p-3f2j-5mp7), prototype pollution via the configuration APIs
  (GHSA-c4c3-pg64-4m4v) and via architecture diagrams (GHSA-3rrr-jr9j-h3q3),
  and CSS injection into siblings of the diagram (GHSA-6x64-9x62-f2gx). The
  preview being network-locked bounds the impact but does not remove it: the
  input is a document, and a document can be malformed or hostile. Taken from
  `cdn.jsdelivr.net/npm/mermaid@11.16.1/dist/mermaid.min.js`; the embedded
  version string was checked but the bundle is otherwise unverified against a
  publisher signature, which npm does not offer for a CDN file.
- **github-markdown-css** embeds nothing, and was long recorded here as
  "unrecorded" on the reasoning that the file had been edited because it opens
  `/*light */`. That inference was wrong: `/*light */` is upstream's own first
  line for the light build, and the vendored file is byte-for-byte identical
  (SHA-256 `de2d14b5…d885`) to
  `github-markdown-css@5.9.0/github-markdown-light.css`. Established on
  2026-08-14 by hashing the two. Nothing was replaced; only the record was
  wrong.
- **pygments-github.css** is not vendored so much as generated, by Pygments'
  own `HtmlFormatter`. It therefore tracks whichever Pygments produced it, not
  a release of its own. Regenerate with:

  ```
  python -c "from pygments.formatters import HtmlFormatter; print(HtmlFormatter(style='default').get_style_defs('.highlight'))"
  ```

## First-party stylesheets (not vendored)

Two files here are written by this project rather than taken from upstream, and so have no version to track. They exist because the C++ port offers a second preview style ("Notes") beside the GitHub one.

- **`notes-light.css`** — the Notes theme's Markdown stylesheet. Not derived from any upstream sheet: it was written against measurements taken from a reference PDF (Segoe UI 9.8pt body in `#1A1A1A`, Semibold headings with no rules, Consolas 9pt in a bordered grey panel, links `#0078C5`). Everything is scoped under `body.theme-notes`, which is load-bearing — the template's own inline `<style>` is emitted after the linked sheets and would otherwise win every tie on `.markdown-body`.
- **`highlight/xcode.min.css`** — the Notes theme's highlight.js palette. highlight.js does ship an `xcode.css`, but **this repo is ITAR-controlled and nothing may be fetched from the network**, so the palette was recovered by measuring the text spans of the same reference PDF and transcribed by hand against highlight.js v11's scope names:

  | Colour | Scope |
  |---|---|
  | `#AA0D91` | keywords |
  | `#C41A16` | strings |
  | `#1C00CF` | numbers, literals |
  | `#007400` | comments |
  | `#5C2699` | identifiers, calls |
  | `#3F6E74` | types, attributes |

  Verified by rendering a tagged fence and reading the span colours back out of the exported PDF, since a wrong scope name fails silently as plain text.

## A note on the C++ port

The port highlights in the browser with highlight.js instead of server-side
with Pygments, so `pygments-github.css` is unused by it while
`highlight/` is unused by the Python app. Both are shipped by both installers
anyway: they are small, and a build that ships only half of them breaks
whichever app it did not anticipate.
