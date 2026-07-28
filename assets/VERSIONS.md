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
| `highlight/highlight.min.js` | 11.11.1 | highlight.js | BSD-3-Clause |
| `highlight/github.min.css` | 11.11.1 | highlight.js, `styles/github.min.css` | BSD-3-Clause |
| `katex/katex.min.js` | 0.17.0 | KaTeX | MIT |
| `katex/katex.min.css` | 0.17.0 | KaTeX | MIT |
| `katex/fonts/*.woff2` (20 files) | 0.17.0 | KaTeX | MIT (fonts: OFL) |
| `mermaid.min.js` | 11.16.0 | mermaid | MIT |
| `github-markdown-light.css` | unrecorded | github-markdown-css | MIT |
| `pygments-github.css` | generated | Pygments `HtmlFormatter(style="default")` | BSD-2-Clause |

## How each version was established

Recorded from the files themselves, not from memory:

- **highlight.js** states it in its own banner: `Highlight.js v11.11.1`. The
  theme has no banner and is only meaningful paired with that release, so it
  carries the same number.
- **KaTeX** embeds `version:"0.17.0"` in the bundle.
- **mermaid** embeds `{version:"11.16.0"}` next to its own accessor. The other
  version string in that file belongs to DOMPurify; see above.
- **github-markdown-css** embeds nothing, and the file has been edited (it
  opens `/*light */`). Its version is genuinely unknown and is left that way
  rather than invented. If it is ever replaced, record the version then.
- **pygments-github.css** is not vendored so much as generated, by Pygments'
  own `HtmlFormatter`. It therefore tracks whichever Pygments produced it, not
  a release of its own. Regenerate with:

  ```
  python -c "from pygments.formatters import HtmlFormatter; print(HtmlFormatter(style='default').get_style_defs('.highlight'))"
  ```

## A note on the C++ port

The port highlights in the browser with highlight.js instead of server-side
with Pygments, so `pygments-github.css` is unused by it while
`highlight/` is unused by the Python app. Both are shipped by both installers
anyway: they are small, and a build that ships only half of them breaks
whichever app it did not anticipate.
