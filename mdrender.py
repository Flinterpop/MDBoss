"""Pure Markdown -> HTML rendering for MDBoss (no Qt imports).

This module is the MDBoss analog of PDF Sherpa's ``tocgen.py``: a small,
Qt-free, unit-testable unit with a single documented public API.  It turns
Markdown source into a self-contained HTML page styled with the bundled
GitHub-light stylesheet, with:

* fenced ``mermaid`` blocks emitted as ``<pre class="mermaid">`` for the
  bundled client-side ``mermaid.min.js`` to render;
* every other fenced block highlighted with Pygments (bundled), matching the
  bundled ``pygments-github.css``;
* headings given stable, GitHub-like slug ``id`` anchors so the outline pane
  can scroll the preview to a heading.

Nothing here reaches the network.  Image ``src`` and link ``href`` values are
left untouched; the page's ``<base href>`` (set by :func:`render_document`)
resolves relative paths against the document's own folder on disk.
"""

from __future__ import annotations

import html
import os
import re
from collections.abc import Sequence
from typing import Any

from markdown_it import MarkdownIt

# A heading entry: (level 1..6, display text, slug id).
Heading = tuple[int, str, str]

# Placeholders substituted into the HTML template.  Chosen so they cannot
# collide with rendered Markdown output.
_PH_BASE = "@@MDBOSS_BASE_HREF@@"
_PH_GH_CSS = "@@MDBOSS_GH_CSS@@"
_PH_PYG_CSS = "@@MDBOSS_PYG_CSS@@"
_PH_MERMAID = "@@MDBOSS_MERMAID_JS@@"
_PH_TITLE = "@@MDBOSS_TITLE@@"
_PH_BODY = "@@MDBOSS_BODY@@"

_SLUG_STRIP = re.compile(r"[^\w\- ]+", re.UNICODE)


def _slug(text: str, seen: dict[str, int]) -> str:
    """GitHub-like heading slug, de-duplicated via ``seen`` (mutated).

    Lowercases, drops punctuation, turns spaces into hyphens, and appends
    ``-1``, ``-2`` ... for repeats so every anchor on a page is unique.
    """
    assert isinstance(text, str), "heading text must be str"
    base = _SLUG_STRIP.sub("", text.strip().lower())
    base = base.replace(" ", "-")
    count = seen.get(base, 0)
    seen[base] = count + 1
    slug = base if count == 0 else f"{base}-{count}"
    assert slug != "" or text == "", "non-empty heading yields a slug"
    return slug


def _highlight_code(code: str, lang: str) -> str | None:
    """Pygments-highlighted HTML for ``lang`` code, or None if no lexer.

    Returns a ``<div class="highlight">...`` block matching the bundled
    ``pygments-github.css``; None signals the caller to fall back to a plain
    ``<pre><code>`` so unknown languages never crash a render.
    """
    if not lang:
        return None
    try:
        from pygments import highlight  # type: ignore[import-untyped]
        from pygments.formatters import (  # type: ignore[import-untyped]
            HtmlFormatter,
        )
        from pygments.lexers import (  # type: ignore[import-untyped]
            get_lexer_by_name,
        )
        from pygments.util import ClassNotFound  # type: ignore[import-untyped]
    except ImportError:
        return None
    try:
        lexer = get_lexer_by_name(lang, stripnl=False)
    except ClassNotFound:
        return None
    return str(highlight(code, lexer, HtmlFormatter(nowrap=False)))


def _render_fence(
    tokens: Sequence[Any], idx: int, options: Any, env: Any
) -> str:
    """Custom markdown-it fence renderer: mermaid, Pygments, or plain.

    ``mermaid`` fences become ``<pre class="mermaid">`` with the raw diagram
    text HTML-escaped (the browser decodes it back to text for mermaid).  All
    other languaged fences go through Pygments; unknown/no-language fences fall
    back to an escaped ``<pre><code>``.
    """
    token = tokens[idx]
    code = token.content
    info = (token.info or "").strip()
    lang = info.split(maxsplit=1)[0].lower() if info else ""
    if lang == "mermaid":
        return f'<pre class="mermaid">{html.escape(code)}</pre>\n'
    highlighted = _highlight_code(code, lang)
    if highlighted is not None:
        return highlighted
    cls = f' class="language-{html.escape(lang)}"' if lang else ""
    return f"<pre><code{cls}>{html.escape(code)}</code></pre>\n"


def _make_md() -> MarkdownIt:
    """Build the shared MarkdownIt instance (GFM-ish, custom fence rule)."""
    md = MarkdownIt("commonmark", {"html": False, "linkify": False})
    md.enable(["table", "strikethrough"])
    md.renderer.rules["fence"] = _render_fence  # type: ignore[attr-defined]
    return md


def _build(md_text: str) -> tuple[str, list[Heading]]:
    """Render ``md_text`` to (body HTML, outline), sharing heading slugs.

    Both outputs are produced from one token pass so the heading ``id`` in the
    HTML and the slug in the outline are guaranteed identical.
    """
    assert isinstance(md_text, str), "markdown source must be str"
    md = _make_md()
    tokens = md.parse(md_text, {})
    outline: list[Heading] = []
    seen: dict[str, int] = {}
    bound = 0
    for i, tok in enumerate(tokens):
        bound += 1
        assert bound <= len(tokens) + 1, "token loop is bounded"
        if tok.type != "heading_open":
            continue
        inline = tokens[i + 1] if i + 1 < len(tokens) else None
        text = inline.content if inline is not None else ""
        slug = _slug(text, seen)
        tok.attrSet("id", slug)
        level = int(tok.tag[1]) if tok.tag[1:].isdigit() else 1
        outline.append((level, text, slug))
    body = md.renderer.render(tokens, md.options, {})
    assert isinstance(body, str), "renderer must return str"
    return body, outline


def extract_outline(md_text: str) -> list[Heading]:
    """Return the heading outline as ``(level, text, slug)`` tuples."""
    _body, outline = _build(md_text)
    return outline


def render_body(md_text: str) -> str:
    """Return just the ``.markdown-body`` inner HTML (no page chrome)."""
    body, _outline = _build(md_text)
    return body


def _load_template() -> str:
    """Read the bundled HTML template beside this module (or in assets/)."""
    here = os.path.dirname(os.path.abspath(__file__))
    for candidate in (
        os.path.join(here, "assets", "template.html"),
        os.path.join(here, "template.html"),
    ):
        if os.path.isfile(candidate):
            with open(candidate, "r", encoding="utf-8") as fh:
                return fh.read()
    raise FileNotFoundError("assets/template.html not found")


def render_document(
    md_text: str,
    base_href: str,
    gh_css_url: str,
    pyg_css_url: str,
    mermaid_js_url: str,
    title: str = "MDBoss",
) -> str:
    """Return a complete HTML page for ``md_text``.

    ``base_href`` is a ``file:///`` URL to the document's folder (with a
    trailing slash) so relative images resolve; the ``*_url`` arguments are
    ``file:///`` URLs to the bundled assets.  Assets are referenced by absolute
    URL so ``<base>`` only affects the document's own relative links.
    """
    assert base_href.endswith("/"), "base_href needs a trailing slash"
    assert md_text is not None, "md_text must not be None"
    body = render_body(md_text)
    page = _load_template()
    page = page.replace(_PH_BASE, html.escape(base_href, quote=True))
    page = page.replace(_PH_GH_CSS, html.escape(gh_css_url, quote=True))
    page = page.replace(_PH_PYG_CSS, html.escape(pyg_css_url, quote=True))
    page = page.replace(_PH_MERMAID, html.escape(mermaid_js_url, quote=True))
    page = page.replace(_PH_TITLE, html.escape(title))
    page = page.replace(_PH_BODY, body)
    assert _PH_BODY not in page, "body placeholder was substituted"
    return page
