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
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from markdown_it import MarkdownIt
from markdown_it.rules_block import StateBlock
from markdown_it.rules_core import StateCore
from markdown_it.token import Token
from mdit_py_plugins.dollarmath import dollarmath_plugin
from mdit_py_plugins.utils import is_code_block

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
_PH_KATEX_CSS = "@@MDBOSS_KATEX_CSS@@"
_PH_KATEX_JS = "@@MDBOSS_KATEX_JS@@"
_PH_TITLE = "@@MDBOSS_TITLE@@"
_PH_BODY = "@@MDBOSS_BODY@@"

# MkDocs / Material admonition type aliases -> canonical (styled) type.
_ADMON_ALIASES = {
    "note": "note",
    "seealso": "note",
    "abstract": "abstract", "summary": "abstract", "tldr": "abstract",
    "info": "info", "todo": "info",
    "tip": "tip", "hint": "tip", "important": "tip",
    "success": "success", "check": "success", "done": "success",
    "question": "question", "help": "question", "faq": "question",
    "warning": "warning", "caution": "warning", "attention": "warning",
    "failure": "failure", "fail": "failure", "missing": "failure",
    "danger": "danger", "error": "danger",
    "bug": "bug",
    "example": "example",
    "quote": "quote", "cite": "quote",
}
# A title wrapped in matching single or double quotes (empty -> no title bar).
_ADMON_QUOTED = re.compile(r"""^(?P<q>["'])(?P<body>.*)(?P=q)\s*$""")

_SLUG_STRIP = re.compile(r"[^\w\- ]+", re.UNICODE)

# HTML sanitiser patterns (see _sanitize_html).
_SCRIPT_RE = re.compile(r"<\s*script\b.*?<\s*/\s*script\s*>", re.I | re.S)
_IFRAME_RE = re.compile(r"<\s*iframe\b.*?<\s*/\s*iframe\s*>", re.I | re.S)
_ON_ATTR_RE = re.compile(
    r"""\son[a-zA-Z]+\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)""", re.I
)
_JS_URL_RE = re.compile(
    r"""(href|src)\s*=\s*("|')\s*javascript:[^"'>]*\2""", re.I
)


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


def _admon_type_and_title(params: str) -> tuple[str, str]:
    """Parse ``params`` after an admonition marker into (canonical, title).

    Handles single- or double-quoted titles (``'x'`` / ``"x"``); an empty
    quoted string suppresses the title bar; a bare type keyword defaults the
    title to that keyword, capitalised (MkDocs/Material behaviour).
    """
    params = params.strip()
    if not params:
        return "note", "Note"
    head, _sep, rest = params.partition(" ")
    canonical = _ADMON_ALIASES.get(head.lower(), head.lower())
    rest = rest.strip()
    if not rest:
        return canonical, head.strip().capitalize()
    match = _ADMON_QUOTED.match(rest)
    if match is not None:
        return canonical, match.group("body")   # may be "" -> no title bar
    return canonical, rest


def _admonition_rule(
    state: StateBlock, start_line: int, end_line: int, silent: bool
) -> bool:
    """Block rule for ``!!!`` / ``???`` / ``???+`` admonitions.

    Adapted from ``mdit_py_plugins.admon`` (MIT).  The indented body is parsed
    as Markdown; collapsible ``???``/``???+`` markers are rendered as native
    ``<details>`` by :func:`_render_admonition`.
    """
    if is_code_block(state, start_line):
        return False
    start = state.bMarks[start_line] + state.tShift[start_line]
    maximum = state.eMarks[start_line]
    if state.src[start] not in "!?":
        return False
    marker = ""
    for candidate in ("???+", "!!!", "???"):
        if state.src[start:start + len(candidate)] == candidate:
            marker = candidate
            break
    if not marker:
        return False
    marker_pos = start + len(marker)
    params = state.src[marker_pos:maximum]
    if not params.strip().split(" ", 1)[0]:
        return False
    if silent:
        return True

    old_parent = state.parentType
    old_line_max = state.lineMax
    old_indent = state.blkIndent

    blk_start = marker_pos
    while blk_start < maximum and state.src[blk_start] == " ":
        blk_start += 1
    state.parentType = "admonition"
    state.blkIndent += blk_start - start + (3 - len(marker))

    was_empty = False
    next_line = start_line
    bound = 0
    while bound <= end_line + 1:              # bounded (Rule of 10)
        bound += 1
        next_line += 1
        if next_line >= end_line:
            break
        pos = state.bMarks[next_line] + state.tShift[next_line]
        line_end = state.eMarks[next_line]
        is_empty = state.sCount[next_line] < state.blkIndent
        if is_empty and was_empty:
            break
        was_empty = is_empty
        if pos < line_end and state.sCount[next_line] < state.blkIndent:
            break
    state.lineMax = next_line

    canonical, title = _admon_type_and_title(params)
    token = state.push("admonition_open", "div", 1)
    token.markup = marker
    token.block = True
    token.attrs = {"class": f"admonition {canonical}"}
    token.map = [start_line, next_line]
    if title:
        t_open = state.push("admonition_title_open", "p", 1)
        t_open.markup = marker
        t_open.attrs = {"class": "admonition-title"}
        t_inline = state.push("inline", "", 0)
        t_inline.content = title
        t_inline.map = [start_line, start_line + 1]
        t_inline.children = []
        state.push("admonition_title_close", "p", -1).markup = marker
    state.md.block.tokenize(state, start_line + 1, next_line)
    close = state.push("admonition_close", "div", -1)
    close.markup = marker
    close.block = True

    state.parentType = old_parent
    state.lineMax = old_line_max
    state.blkIndent = old_indent
    state.line = next_line
    return True


def _render_admonition(tokens: Sequence[Any], idx: int, options: Any,
                       env: Any) -> str:
    """Render admonition tokens as ``<div>`` or native ``<details>``."""
    token = tokens[idx]
    marker = token.markup
    collapsible = marker.startswith("?")
    kind = token.type
    if kind == "admonition_open":
        cls = html.escape(token.attrs.get("class", ""), quote=True)
        if collapsible:
            open_attr = " open" if marker.endswith("+") else ""
            return f'<details class="{cls}"{open_attr}>\n'
        return f'<div class="{cls}">\n'
    if kind == "admonition_close":
        return "</details>\n" if collapsible else "</div>\n"
    if kind == "admonition_title_open":
        if collapsible:
            return '<summary class="admonition-title">'
        return '<p class="admonition-title">'
    return "</summary>\n" if collapsible else "</p>\n"


def _render_math_inline(tokens: Sequence[Any], idx: int, options: Any,
                        env: Any) -> str:
    """Render an inline ``$...$`` math token for client-side KaTeX."""
    body = html.escape(tokens[idx].content)
    return f'<span class="math-inline">{body}</span>'


def _render_math_block(tokens: Sequence[Any], idx: int, options: Any,
                       env: Any) -> str:
    """Render a ``$$...$$`` display-math token for client-side KaTeX."""
    body = html.escape(tokens[idx].content)
    return f'<div class="math-display">{body}</div>\n'


def _make_md() -> MarkdownIt:
    """Build the shared MarkdownIt instance (GFM-ish, custom rules).

    Raw HTML is enabled so embeds like ``<img src="pic.png" style="zoom:40%">``
    render; :func:`_sanitize_html` then strips active content, and the preview
    web view is network-locked, so this stays safe for local documents.
    """
    md = MarkdownIt("commonmark", {"html": True, "linkify": False})
    md.enable(["table", "strikethrough"])
    md.use(dollarmath_plugin, double_inline=True)
    md.core.ruler.before("inline", "github_alerts", _github_alerts)
    md.block.ruler.before(
        "fence", "admonition", _admonition_rule,
        {"alt": ["paragraph", "reference", "blockquote", "list"]},
    )
    rules = md.renderer.rules  # type: ignore[attr-defined]
    rules["fence"] = _render_fence
    rules["math_inline"] = _render_math_inline
    rules["math_block"] = _render_math_block
    for name in (
        "admonition_open", "admonition_close",
        "admonition_title_open", "admonition_title_close",
    ):
        rules[name] = _render_admonition
    return md


def _sanitize_html(body: str) -> str:
    """Strip active/dangerous HTML from rendered output (defense-in-depth).

    Removes ``<script>``/``<iframe>`` blocks, inline ``on*`` event-handler
    attributes, and ``javascript:`` URLs.  Static formatting and media
    (``img``, ``div``, ``span``, tables, inline styles) are preserved so
    Typora/GitHub style HTML embeds render normally.
    """
    assert isinstance(body, str), "body must be str"
    body = _SCRIPT_RE.sub("", body)
    body = _IFRAME_RE.sub("", body)
    body = _ON_ATTR_RE.sub("", body)
    body = _JS_URL_RE.sub(r'\1="#"', body)
    return body


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
    body = _sanitize_html(md.renderer.render(tokens, md.options, {}))
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


def _asset_dir() -> str:
    """Directory holding bundled render assets (source tree or frozen exe)."""
    base = getattr(
        sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__))
    )
    return os.path.join(base, "assets")


def _asset_uri(name: str) -> str:
    """A ``file:///`` URL to a bundled asset, for the web view."""
    return Path(os.path.join(_asset_dir(), name)).as_uri()


def _load_template() -> str:
    """Read the bundled HTML template from the assets directory."""
    path = os.path.join(_asset_dir(), "template.html")
    if not os.path.isfile(path):
        raise FileNotFoundError("assets/template.html not found")
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def render_document(
    md_text: str,
    base_href: str,
    title: str = "MDBoss",
    strip_yaml: bool = False,
) -> str:
    """Return a complete HTML page for ``md_text``.

    ``base_href`` is a ``file:///`` URL to the document's folder (with a
    trailing slash) so relative images resolve.  Bundled asset URLs (GitHub
    CSS, Pygments CSS, mermaid, KaTeX) are resolved here and referenced by
    absolute URL, so ``<base>`` only affects the document's own relative
    links.  When ``strip_yaml`` is true, a leading YAML front-matter block is
    not rendered.
    """
    assert base_href.endswith("/"), "base_href needs a trailing slash"
    assert md_text is not None, "md_text must not be None"
    body = render_body(md_text, strip_yaml)
    page = _load_template()
    replacements = {
        _PH_BASE: base_href,
        _PH_GH_CSS: _asset_uri("github-markdown-light.css"),
        _PH_PYG_CSS: _asset_uri("pygments-github.css"),
        _PH_MERMAID: _asset_uri("mermaid.min.js"),
        _PH_KATEX_CSS: _asset_uri("katex/katex.min.css"),
        _PH_KATEX_JS: _asset_uri("katex/katex.min.js"),
    }
    for placeholder, value in replacements.items():
        page = page.replace(placeholder, html.escape(value, quote=True))
    page = page.replace(_PH_TITLE, html.escape(title))
    page = page.replace(_PH_BODY, body)
    assert _PH_BODY not in page, "body placeholder was substituted"
    return page
