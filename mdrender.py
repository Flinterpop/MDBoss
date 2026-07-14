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
from markdown_it.rules_core import StateCore
from markdown_it.token import Token

# A heading entry: (level 1..6, display text, slug id).
Heading = tuple[int, str, str]

# GitHub-style alerts: a blockquote whose first line is [!NOTE] (or TIP,
# IMPORTANT, WARNING, CAUTION) renders as a coloured callout box.  The
# matching CSS ships in assets/github-markdown-light.css and the octicon
# is injected by assets/template.html.
_ALERT_RE = re.compile(
    r"^[ \t]*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\][ \t]*(?:\r?\n|$)",
    re.IGNORECASE,
)
_ALERT_LABELS = {
    "note": "Note",
    "tip": "Tip",
    "important": "Important",
    "warning": "Warning",
    "caution": "Caution",
}

# A leading YAML (``---``) or TOML (``+++``) front-matter block.  Anchored at
# the start of the file so a horizontal rule further down is never removed.
_FRONT_MATTER_RE = re.compile(
    "\\A﻿?"                                   # start, optional BOM
    + r"(?:---|\+\+\+)[ \t]*\r?\n"                 # opening fence
    + r"(?:.*?\r?\n)?"                             # front-matter body
    + r"(?:---|\+\+\+|\.\.\.)[ \t]*(?:\r?\n|\Z)",  # closing fence
    re.DOTALL,
)

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


def _close_blockquote_as_div(tokens: list[Token], open_idx: int) -> None:
    """Retag the ``blockquote_close`` matching the open at ``open_idx``."""
    depth = 0
    bound = 0
    for j in range(open_idx, len(tokens)):
        bound += 1
        assert bound <= len(tokens), "close scan is bounded"
        if tokens[j].type == "blockquote_open":
            depth += 1
        elif tokens[j].type == "blockquote_close":
            depth -= 1
            if depth == 0:
                tokens[j].tag = "div"
                return


def _github_alerts(state: StateCore) -> None:
    """Turn ``> [!NOTE]`` blockquotes into GitHub alert callout divs.

    Runs before inline parsing, so it only edits token content/tags; the
    marker line is stripped and a ``markdown-alert-title`` paragraph inserted.
    The container's ``blockquote_open``/``blockquote_close`` become ``div``.
    """
    tokens = state.tokens
    i = 0
    bound = 0
    while i < len(tokens):
        bound += 1
        assert bound <= len(tokens) + 100000, "alert scan is bounded"
        is_alert_head = (
            tokens[i].type == "blockquote_open"
            and i + 2 < len(tokens)
            and tokens[i + 1].type == "paragraph_open"
            and tokens[i + 2].type == "inline"
        )
        if not is_alert_head:
            i += 1
            continue
        inline = tokens[i + 2]
        match = _ALERT_RE.match(inline.content)
        if match is None:
            i += 1
            continue
        kind = match.group(1).lower()
        tokens[i].tag = "div"
        tokens[i].attrSet("class", f"markdown-alert markdown-alert-{kind}")
        _close_blockquote_as_div(tokens, i)
        title = Token("html_block", "", 0)
        title.content = (
            f'<p class="markdown-alert-title">{_ALERT_LABELS[kind]}</p>\n'
        )
        rest = inline.content[match.end():]
        if rest.strip() == "":
            del tokens[i + 1:i + 4]        # drop the marker-only paragraph
            tokens.insert(i + 1, title)
        else:
            inline.content = rest          # inline rule parses the remainder
            tokens.insert(i + 1, title)
        i += 1


def _make_md() -> MarkdownIt:
    """Build the shared MarkdownIt instance (GFM-ish, custom rules)."""
    md = MarkdownIt("commonmark", {"html": False, "linkify": False})
    md.enable(["table", "strikethrough"])
    md.renderer.rules["fence"] = _render_fence  # type: ignore[attr-defined]
    md.core.ruler.before("inline", "github_alerts", _github_alerts)
    return md


def strip_front_matter(md_text: str) -> str:
    """Remove a leading YAML/TOML front-matter block, if present.

    A front-matter block is a ``---`` (or ``+++``) fence at the very start of
    the file, its lines, and a closing ``---``/``+++``/``...`` fence.  Only a
    block anchored at position 0 is removed; a horizontal rule further down is
    untouched.
    """
    assert isinstance(md_text, str), "markdown source must be str"
    return _FRONT_MATTER_RE.sub("", md_text, count=1)


def _build(
    md_text: str, strip_yaml: bool = False
) -> tuple[str, list[Heading]]:
    """Render ``md_text`` to (body HTML, outline), sharing heading slugs.

    Both outputs are produced from one token pass so the heading ``id`` in the
    HTML and the slug in the outline are guaranteed identical.  When
    ``strip_yaml`` is true, a leading YAML front-matter block is removed first.
    """
    assert isinstance(md_text, str), "markdown source must be str"
    if strip_yaml:
        md_text = strip_front_matter(md_text)
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


def extract_outline(md_text: str, strip_yaml: bool = False) -> list[Heading]:
    """Return the heading outline as ``(level, text, slug)`` tuples."""
    _body, outline = _build(md_text, strip_yaml)
    return outline


def render_body(md_text: str, strip_yaml: bool = False) -> str:
    """Return just the ``.markdown-body`` inner HTML (no page chrome)."""
    body, _outline = _build(md_text, strip_yaml)
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
    strip_yaml: bool = False,
) -> str:
    """Return a complete HTML page for ``md_text``.

    ``base_href`` is a ``file:///`` URL to the document's folder (with a
    trailing slash) so relative images resolve; the ``*_url`` arguments are
    ``file:///`` URLs to the bundled assets.  Assets are referenced by absolute
    URL so ``<base>`` only affects the document's own relative links.  When
    ``strip_yaml`` is true, a leading YAML front-matter block is not rendered.
    """
    assert base_href.endswith("/"), "base_href needs a trailing slash"
    assert md_text is not None, "md_text must not be None"
    body = render_body(md_text, strip_yaml)
    page = _load_template()
    page = page.replace(_PH_BASE, html.escape(base_href, quote=True))
    page = page.replace(_PH_GH_CSS, html.escape(gh_css_url, quote=True))
    page = page.replace(_PH_PYG_CSS, html.escape(pyg_css_url, quote=True))
    page = page.replace(_PH_MERMAID, html.escape(mermaid_js_url, quote=True))
    page = page.replace(_PH_TITLE, html.escape(title))
    page = page.replace(_PH_BODY, body)
    assert _PH_BODY not in page, "body placeholder was substituted"
    return page
