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
  folder.
- **Code**: fenced blocks are syntax-highlighted (Pygments).

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
