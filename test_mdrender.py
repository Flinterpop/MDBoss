"""Unit tests for the pure ``mdrender`` module (no Qt required)."""

from __future__ import annotations

import mdrender

SAMPLE = """# Title

Intro with **bold**, *italic*, and `code`.

## Section Two

| A | B |
|---|---|
| 1 | 2 |

```python
def f(x):
    return x + 1
```

```mermaid
graph TD
  A[Start] --> B{OK?}
```

![pic](images/pic.png)

## Section Two
"""


def test_outline_levels_and_slugs() -> None:
    outline = mdrender.extract_outline(SAMPLE)
    assert outline[0] == (1, "Title", "title")
    assert outline[1] == (2, "Section Two", "section-two")
    # Duplicate heading gets a de-duplicated slug.
    assert outline[2] == (2, "Section Two", "section-two-1")


def test_body_contains_expected_html() -> None:
    body = mdrender.render_body(SAMPLE)
    assert '<pre class="mermaid">' in body
    assert "graph TD" in body
    assert 'class="highlight"' in body          # Pygments block
    assert "<table>" in body
    assert 'id="section-two"' in body
    assert '<img src="images/pic.png"' in body


def test_render_document_substitutes_all_placeholders() -> None:
    page = mdrender.render_document(
        SAMPLE,
        base_href="file:///c:/docs/",
        gh_css_url="file:///a/github.css",
        pyg_css_url="file:///a/pyg.css",
        mermaid_js_url="file:///a/mermaid.min.js",
        title="Doc",
    )
    assert "@@MDBOSS" not in page
    assert 'href="file:///c:/docs/"' in page
    assert "file:///a/mermaid.min.js" in page
    assert "<title>Doc</title>" in page


def test_mermaid_content_is_escaped() -> None:
    body = mdrender.render_body("```mermaid\nA-->B\n```\n")
    # Arrow is HTML-escaped so it survives as text for mermaid to parse.
    assert "A--&gt;B" in body


def test_empty_input_is_safe() -> None:
    assert mdrender.extract_outline("") == []
    assert isinstance(mdrender.render_body(""), str)


def test_unknown_language_falls_back_to_plain_code() -> None:
    body = mdrender.render_body("```nosuchlang\nhi\n```\n")
    assert "<pre><code" in body
    assert "hi" in body


ALERT = """> [!NOTE]
>
> Run this from an administrator prompt.

> [!WARNING]
> Careful with `rm`.
"""


def test_github_alerts_render_as_callouts() -> None:
    body = mdrender.render_body(ALERT)
    assert '<div class="markdown-alert markdown-alert-note">' in body
    assert '<div class="markdown-alert markdown-alert-warning">' in body
    assert '<p class="markdown-alert-title">Note</p>' in body
    assert "Run this from an administrator prompt." in body
    assert "<code>rm</code>" in body          # inline still parsed
    assert "[!NOTE]" not in body              # marker removed
    assert "<blockquote>" not in body         # not a plain blockquote


def test_unknown_alert_type_stays_a_blockquote() -> None:
    body = mdrender.render_body("> [!BOGUS]\n> text\n")
    assert "markdown-alert" not in body
    assert "<blockquote>" in body


FRONT = """---
title: My Doc
tags: [a, b]
---

# Heading

Body.
"""


def test_front_matter_kept_by_default() -> None:
    body = mdrender.render_body(FRONT)
    assert "title: My Doc" in body


def test_front_matter_stripped_when_requested() -> None:
    body = mdrender.render_body(FRONT, strip_yaml=True)
    assert "title: My Doc" not in body
    assert 'id="heading"' in body
    outline = mdrender.extract_outline(FRONT, strip_yaml=True)
    assert outline == [(1, "Heading", "heading")]


def test_horizontal_rule_not_treated_as_front_matter() -> None:
    md = "# A\n\ntext\n\n---\n\nmore\n"
    assert "<hr" in mdrender.render_body(md, strip_yaml=True)


def test_raw_html_img_embed_renders() -> None:
    md = (
        '<img src="pics/a.png" alt="x" style="zoom: 40%;" />\n\n'
        "and <b>inline</b> html\n"
    )
    body = mdrender.render_body(md)
    assert '<img src="pics/a.png"' in body
    assert "zoom: 40%" in body           # inline style preserved
    assert "<b>inline</b>" in body


def test_sanitizer_strips_active_content() -> None:
    md = (
        "<script>alert(1)</script>\n\n"
        '<img src=x onerror="alert(2)">\n\n'
        '<a href="javascript:alert(3)">x</a>\n\n'
        '<iframe src="http://evil"></iframe>\n'
    )
    body = mdrender.render_body(md).lower()
    assert "<script" not in body
    assert "onerror" not in body
    assert "javascript:" not in body
    assert "<iframe" not in body
    assert "<img src=x" in body          # the safe image survives
