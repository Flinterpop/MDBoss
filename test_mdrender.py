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
