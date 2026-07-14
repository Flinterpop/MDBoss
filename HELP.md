# MD Boss

A local Markdown manager, editor, and GitHub-style viewer. Browse Markdown
files across up to **five root folders**, edit them beside a **live preview**
rendered with the **GitHub-light** theme, **mermaid** diagrams, and embedded
images — all **100% offline**.

## Getting started

1. Click **Manage folders…** and add one to five root folders. Each root
   appears as a top-level node in the file tree.
2. Click a `.md` file to open it. The source appears on the left, the rendered
   preview on the right.
3. Type in the editor — the preview updates automatically as you pause.
4. Press **Ctrl+S** to save.

## Panes

- **Files** (left) — your roots as a combined tree. Filter with the box at the
  top. Right-click for New file/folder, Rename, Delete (to Recycle Bin),
  Reveal in Explorer, Copy path, and Favorite.
- **Outline** (middle) — headings of the current document. Click a heading to
  scroll the preview to it. **Favorites** below it hold up to ten documents.
- **Editor \| Preview** (right) — the source editor beside the live preview.
  Use the **Edit** toolbar button to hide the editor for distraction-free
  reading.

## Rendering

- **Mermaid**: fence a diagram with a ` ```mermaid ` block.
- **Images**: `![alt](relative/path.png)` resolves against the document's own
  folder. Raw HTML embeds work too, including Typora-style sizing —
  `<img src="pics/a.png" style="zoom: 40%;" />`.
- **Raw HTML**: inline/block HTML in a document is rendered (active content
  such as `<script>` is stripped for safety).
- **Code**: fenced blocks are syntax-highlighted (Pygments).
- **Alerts**: a blockquote beginning with `[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`,
  `[!WARNING]`, or `[!CAUTION]` renders as a coloured GitHub callout box:

  ```
  > [!NOTE]
  > This step must be run as administrator.
  ```
- **Admonitions** (MkDocs / Material): `!!! type "Title"` renders a coloured
  callout; `??? type "Title"` is collapsible (collapsed), `???+` opens by
  default. Types include note, info, tip, success, question, warning, failure,
  danger, bug, example, quote (and aliases). An empty `""` title hides the
  title bar. The body is indented four spaces:

  ```
  !!! warning "Heads up"
      Indented body with **Markdown**.
  ```
- **Math** (LaTeX): inline `$E = mc^2$` and display `$$ \int_0^1 x^2\,dx $$`,
  rendered with the bundled KaTeX.

## Toolbar toggles

- **Edit** — show/hide the source editor (preview-only reading mode).
- **Files / Outline** — show/hide those panes.
- **Hide YAML** — when on (the default), a leading YAML front-matter block
  (`--- … ---` at the very top of the file) is not shown in the preview.

## Scrolling

The editor and preview scroll together in both directions: scroll or type in
the source pane and the preview follows; scroll the preview and the editor
follows. The position is matched proportionally, so it stays roughly aligned
even though the source and rendered document differ in height.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N   | New file |
| Ctrl+S   | Save |
| F5       | Refresh the tree |
| F1       | This help |

## Privacy

The preview's web view is **network-locked**: any remote image, script, or
link inside a document is blocked, so document content never leaves your
machine. The only network request MD Boss ever makes is an optional version
check against its own GitHub releases page.
