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

You do not have to add a root folder to read a document. **Ctrl+O** opens any
Markdown file on disk, dragging one onto the window opens it, and MD Boss opens
files passed on the command line — so it works as a plain Markdown viewer as
well as a library manager. Roots are for browsing; everything else works
without them.

## Panes

- **Left pane** — **Recent** and **Favorites** sit at the top (see below), above
  the **Files** list: your roots as a combined tree, where each folder shows the
  number of Markdown files within it (counted recursively). Folders with no
  Markdown files anywhere inside them are not listed. Filter with the box above
  the tree. Right-click a file for New file/folder, Rename, Delete (to the
  Recycle Bin / Trash), Reveal in Explorer / Show in file manager, Copy path,
  and Favorite.
- **Outline** (middle) — headings of the current document. Click a heading to
  scroll the preview to it.
- **Recent** (top of the left pane) — the last six documents you opened, newest
  first, kept automatically. Re-opening one moves it back to the top rather than
  adding a duplicate. Click to reopen; right-click to open, favorite, reveal, or
  copy a path, and use the **⋯** menu to clear the list.
- **Favorites** (below Recent) — pin up to ten documents, newest first
  (adding an 11th drops the oldest). Drag the divider below it to make the
  panel taller or shorter. Rows show the filename (hover for the full path);
  missing files show in red. Right-click a file in the tree to add or remove
  it; right-click a favorite to open, remove, reveal, or copy its path; and use
  the **⋯** menu in the Favorites header to export, import, or clear the list.
  Importing a favorites file exported on Windows remaps its drive-letter paths
  (e.g. `J:\Dropbox\…`) onto your home folder when you're on Linux.
- **Editor \| Preview** (right) — the source editor beside the live preview.
  Use the **Edit** toolbar button to hide the editor for distraction-free
  reading.

## Opening files from outside your folders

- **Ctrl+O** (**Open…**) — browse for any Markdown file on disk. It need not be
  under a root folder; it opens in place and is not copied anywhere.
- **Drag and drop** — drop a `.md` file from Explorer onto the window and it
  opens where it lies. Dropping several opens the first.
- **Command line** — `MDBoss.exe "C:\path\to\note.md"` opens that document. This
  is what makes MD Boss usable as the Windows default app for `.md` files.

MD Boss keeps a **single window**. Opening a second document from Explorer or
the command line hands it to the window already running and brings it to the
front, rather than starting a second copy — which also stops two copies from
overwriting each other's recent list and layout on exit.

Documents opened this way appear in **Recent**, which is the easiest way back to
a file that lives outside your root folders. Use **Add to favorites** to keep
one for good.

## Making MD Boss your Markdown app

Click **File types…** on the toolbar and press **Register**. That adds MD Boss
to the **Open with** menu for `.md`, `.markdown`, `.mdown`, `.mkd` and `.mdwn`,
and lists it in **Settings → Default apps**. The installer offers the same thing
as a checkbox, and uninstalling removes it again.

Windows does not let any application make itself the default for a file type,
so one step is yours and cannot be automated: right-click a Markdown file,
choose **Open with → Choose another app**, pick MD Boss and tick **Always**.
The **Windows default apps…** button in the dialog opens the settings page for
you.

All of this is written under your own user account (`HKEY_CURRENT_USER`) — no
administrator rights, and nothing is changed for other users of the machine.
**Remove** undoes it; other Markdown editors registered on the machine are left
alone.

If you move a portable copy of MD Boss to a different folder, open **File
types…** again and press **Re-register** so Windows points at the new location.

## MD_Inbox

Create a folder named **`MD_Inbox`** in one of your root folders (or add a root
that is itself named `MD_Inbox`) to have a defined landing place for incoming
documents. Right-click in the **Files** pane and choose
**Import files into MD_Inbox…** to copy files into it: the originals stay put,
the tree refreshes, and the first file opens. A name that already exists in the
inbox is kept — the copy is saved as `name (2).md` rather than overwriting.

Dragging a file onto the window no longer copies it here — it opens the file
where it lies, so MD Boss works as a plain viewer. Use the menu command above
when you do want a copy in the inbox.

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

- **Files** — show/hide the left pane (Recent + Favorites + file list).
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

Templates are plain `.md` files in `%APPDATA%\MDBoss\templates` on Windows
(`~/.config/MDBoss/templates` on Linux); use **New file ▸ Manage templates…**
to open the folder. A couple of starters are
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

MD Boss checks its own GitHub releases page on launch (on Windows, and on
Linux when run as the AppImage). When a newer version is available it offers to
**download and install it now**: on Windows it fetches the installer, quits,
installs silently, and restarts on the new version; the Linux AppImage
downloads the new AppImage, replaces itself in place, and relaunches. You can
also check any time from **Help → Check for updates**. Choose *No* to skip a
version or *Cancel* to be reminded next launch. A source or virtualenv run
can't self-update, so there the manual check just points you at the releases
page.

## Privacy

The preview's web view is **network-locked**: any remote image, script, or
link inside a document is blocked, so document content never leaves your
machine. The only network requests MD Boss makes are the optional update check
(and downloading an update if you accept one) against its own GitHub releases
page — never anything from your documents.
