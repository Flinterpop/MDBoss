"""Generate the golden corpus for the C++ mdrender port.

The Python renderer at the repo root is the parity oracle for the C++ port, so
the expected output is produced *by it* rather than written by hand.  Each case
emits three files into ``golden/``:

* ``<name>.md``       -- the Markdown source
* ``<name>.html``     -- ``mdrender.render_body()`` output
* ``<name>.outline``  -- one ``level<TAB>text<TAB>slug`` line per heading

plus ``index.tsv`` listing ``name<TAB>strip_yaml`` so the Catch2 test can walk
the corpus without hard-coding case names.

Run from anywhere::

    python MDBossCpp/tests/make_golden.py

Re-run it whenever ``mdrender.py`` changes; a diff in ``golden/`` is then a
deliberate record of the behaviour change, reviewed like any other.
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

import mdrender  # noqa: E402  (needs the sys.path line above)

_GOLDEN_DIR = os.path.join(_HERE, "golden")

# (name, markdown source, strip_yaml).  These mirror the inputs already
# exercised by the repo-root test_mdrender.py, so every custom rule in the
# Python renderer has a golden case here: alerts, admonitions, math, the
# sanitiser, front matter, mermaid escaping, slug de-duplication and the
# unknown-language fence fallback.
_CASES: list[tuple[str, str, bool]] = [
    (
        "basic",
        "# Title\n"
        "\n"
        "Intro with **bold**, *italic*, and `code`.\n"
        "\n"
        "## Section Two\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "```python\n"
        "def f(x):\n"
        "    return x + 1\n"
        "```\n"
        "\n"
        "```mermaid\n"
        "graph TD\n"
        "  A[Start] --> B{OK?}\n"
        "```\n"
        "\n"
        "![pic](images/pic.png)\n"
        "\n"
        "## Section Two\n",
        False,
    ),
    ("empty", "", False),
    ("mermaid_escape", "```mermaid\nA-->B\n```\n", False),
    ("unknown_language", "```nosuchlang\nhi\n```\n", False),
    ("plain_fence", "```\njust text\n```\n", False),
    (
        "alerts",
        "> [!NOTE]\n"
        ">\n"
        "> Run this from an administrator prompt.\n"
        "\n"
        "> [!WARNING]\n"
        "> Careful with `rm`.\n",
        False,
    ),
    ("alert_unknown_type", "> [!BOGUS]\n> text\n", False),
    (
        "alert_inline_text",
        "> [!TIP] Same line **content** here.\n"
        "> And a second line.\n",
        False,
    ),
    (
        "front_matter_kept",
        "---\ntitle: My Doc\ntags: [a, b]\n---\n\n# Heading\n\nBody.\n",
        False,
    ),
    (
        "front_matter_stripped",
        "---\ntitle: My Doc\ntags: [a, b]\n---\n\n# Heading\n\nBody.\n",
        True,
    ),
    (
        "front_matter_toml",
        "+++\ntitle = 'x'\n+++\n\n# TOML Heading\n",
        True,
    ),
    ("horizontal_rule", "# A\n\ntext\n\n---\n\nmore\n", True),
    (
        "raw_html",
        '<img src="pics/a.png" alt="x" style="zoom: 40%;" />\n'
        "\n"
        "and <b>inline</b> html\n",
        False,
    ),
    (
        "admonitions",
        "!!! warning 'Optional Title'\n"
        "    Block-Styled Side Content with **Markdown support**\n"
        "\n"
        "!!! info ''\n"
        "    No-Heading Content\n"
        "\n"
        "??? bug 'Collapsed by default'\n"
        "    Collapsible Block-Styled Side Content\n"
        "\n"
        '???+ example "Open by default"\n'
        "    Open collapsible content\n",
        False,
    ),
    ("admonition_alias", "!!! hint\n    tipped\n", False),
    # A closing </div> begins an HTML block, and an HTML block runs until a
    # blank line.  These two pin down that the constructs following an alert
    # or an admonition are still parsed as Markdown rather than swallowed.
    (
        "admonition_then_fence",
        "!!! warning \"Title\"\n"
        "    Body text.\n"
        "\n"
        "```mermaid\n"
        "graph TD\n"
        "  A --> B\n"
        "```\n"
        "\n"
        "## After\n",
        False,
    ),
    (
        "alert_then_heading",
        "> [!NOTE]\n> Alert body.\n\n## Heading After\n\ntext\n",
        False,
    ),
    ("admonition_bare", "!!! note\n    plain note body\n", False),
    (
        "math",
        "Inline $E=mc^2$ text.\n\n$$\\int_0^1 x^2\\,dx$$\n",
        False,
    ),
    (
        "sanitizer",
        "<script>alert(1)</script>\n"
        "\n"
        '<img src=x onerror="alert(2)">\n'
        "\n"
        '<a href="javascript:alert(3)">x</a>\n'
        "\n"
        '<iframe src="http://evil"></iframe>\n',
        False,
    ),
    (
        "slug_dedup",
        "# Repeat\n\n## Repeat\n\n### Repeat\n\n#### Other Heading!\n",
        False,
    ),
    (
        "heading_markup",
        "## Section **Two** and `code`\n\n### Plain\n",
        False,
    ),
    # The slug comes from the *raw* heading source, not from the rendered
    # text, so a link in a heading slugs its URL characters away rather than
    # keeping only the label.  This case pins that down: deriving slugs from
    # parsed text would give "a-link" instead.
    (
        "heading_link",
        "## A [link](http://x/y) here\n\n## Trailing punctuation!?\n",
        False,
    ),
    ("heading_setext", "Setext One\n===\n\nSetext Two\n---\n", False),
    # A '#' inside a fenced block is not a heading; the outline must be empty.
    (
        "heading_in_fence",
        "```\n# Not A Heading\n```\n\n    # Indented, also not\n",
        False,
    ),
    ("heading_all_levels", "# a\n## b\n### c\n#### d\n##### e\n###### f\n",
     False),
    (
        "nested_structure",
        "# Top\n"
        "\n"
        "- item one\n"
        "  - nested with `code`\n"
        "- item two\n"
        "\n"
        "> plain blockquote\n"
        ">\n"
        "> second para\n"
        "\n"
        "1. ordered\n"
        "2. list\n"
        "\n"
        "~~struck~~ and [a link](https://example.com/page).\n",
        False,
    ),
]

# Real documents from the repo, added so the corpus is not only synthetic.
_REAL_DOCS: list[str] = ["README.md", "HELP.md"]


def _outline_text(md_text: str, strip_yaml: bool) -> str:
    """Serialise the outline as ``level<TAB>text<TAB>slug`` lines."""
    lines = []
    for level, text, slug in mdrender.extract_outline(md_text, strip_yaml):
        assert "\t" not in text, "heading text must not contain a tab"
        lines.append(f"{level}\t{text}\t{slug}")
    return "".join(line + "\n" for line in lines)


def _write(path: str, text: str) -> None:
    """Write ``text`` as UTF-8 with LF endings, so goldens are stable."""
    assert isinstance(text, str), "golden content must be str"
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def _emit(name: str, md_text: str, strip_yaml: bool) -> None:
    """Emit the three golden files for one case."""
    assert name, "case needs a name"
    _write(os.path.join(_GOLDEN_DIR, name + ".md"), md_text)
    _write(
        os.path.join(_GOLDEN_DIR, name + ".html"),
        mdrender.render_body(md_text, strip_yaml),
    )
    _write(
        os.path.join(_GOLDEN_DIR, name + ".outline"),
        _outline_text(md_text, strip_yaml),
    )


def _load_real_docs() -> list[tuple[str, str, bool]]:
    """Read the repo's own Markdown docs as extra corpus cases."""
    cases: list[tuple[str, str, bool]] = []
    for filename in _REAL_DOCS:
        path = os.path.join(_REPO_ROOT, filename)
        if not os.path.isfile(path):
            print(f"  skipped (missing): {filename}")
            continue
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        stem = "doc_" + os.path.splitext(filename)[0].lower()
        cases.append((stem, text, True))
    return cases


def main() -> int:
    """Regenerate the whole corpus.  Returns a process exit code."""
    os.makedirs(_GOLDEN_DIR, exist_ok=True)
    cases = [*_CASES, *_load_real_docs()]
    names = [name for name, _text, _strip in cases]
    assert len(names) == len(set(names)), "case names must be unique"

    index = []
    for name, md_text, strip_yaml in cases:
        _emit(name, md_text, strip_yaml)
        index.append(f"{name}\t{1 if strip_yaml else 0}")
    _write(
        os.path.join(_GOLDEN_DIR, "index.tsv"),
        "".join(line + "\n" for line in index),
    )
    print(f"wrote {len(cases)} cases to {_GOLDEN_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
