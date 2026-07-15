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

- **Left pane** — **Favorites** are pinned at the top (see below), above the
  **Files** list: your roots as a combined tree, where each folder shows the
  number of Markdown files within it (counted recursively). Filter with the
  box above the tree. Right-click a file for New file/folder, Rename, Delete
  (to Recycle Bin), Reveal in Explorer, Copy path, and Favorite.
- **Outline** (middle) — headings of the current document. Click a heading to
  scroll the preview to it.
- **Favorites** (top of the left pane) — pin up to ten documents, newest first
  (adding an 11th drops the oldest). Drag the divider below it to make the
  panel taller or shorter. Rows show the filename (hover for the full path);
  missing files show in red. Right-click a file in the tree to add or remove
  it; right-click a favorite to open, remove, reveal, or copy its path; and use
  the **⋯** menu in the Favorites header to export, import, or clear the list.
- **Editor \| Preview** (right) — the source editor beside the live preview.
  Use the **Edit** toolbar button to hide the editor for distraction-free
  reading.

## Drag and drop

Create a folder named **`MD_Inbox`** in one of your root folders (or add a root
that is itself named `MD_Inbox`), and MD Boss becomes a drop target for Markdown
files. Drag one or more `.md` files from Explorer onto the window and they are
**copied into `MD_Inbox`** (the originals stay put), the tree refreshes, and the
first dropped file opens. A name that already exists in the inbox is kept — the
copy is saved as `name (2).md` rather than overwriting. Without an `MD_Inbox`
folder, drops are ignored.

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

Ordered to match the columns — **Files**, **Outline**, **Edit**:

- **Files** — show/hide the left pane (Favorites + file list).
- **Outline** — show/hide the outline column.
- **Edit** — show/hide the source editor (preview-only reading mode).
- **Hide YAML** — when on (the default), a leading YAML front-matter block
  (`--- … ---` at the very top of the file) is not shown in the preview.

## Scrolling

The editor and preview scroll together in both directions: scroll or type in
the source pane and the preview follows; scroll the preview and the editor
follows. The position is matched proportionally, so it stays roughly aligned
even though the source and rendered document differ in height.

## Templates

New files can start from a template:

- **New from template…** on the toolbar opens a template as a new (unsaved)
  buffer.
- Right-click a folder → **New file ▸** and pick **Blank** or a template; the
  file is created in that folder.

Templates are plain `.md` files in `%APPDATA%\MDBoss\templates` (use **New
file ▸ Manage templates…** to open the folder). A couple of starters are
created on first run. Templates may use these placeholders, filled in when the
file is created:

| Placeholder | Becomes |
|-------------|---------|
| `{{title}}` | the new file's name (or "New document") |
| `{{date}}`  | today's date, `YYYY-MM-DD` |
| `{{time}}`  | the current time, `HH:MM` |
| `{{datetime}}` | date and time |

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N   | New file |
| Ctrl+S   | Save |
| F5       | Refresh the tree |
| F1       | This help |

## Updates

MD Boss checks its own GitHub releases page on launch. When a newer version is
available it offers to **download and install it now** — it fetches the
installer, quits, installs silently, and restarts on the new version. You can
also check any time from **Help → Check for updates**. Choose *No* to skip a
version or *Cancel* to be reminded next launch.

## Privacy

The preview's web view is **network-locked**: any remote image, script, or
link inside a document is blocked, so document content never leaves your
machine. The only network requests MD Boss makes are the optional update check
(and downloading an update if you accept one) against its own GitHub releases
page — never anything from your documents.
