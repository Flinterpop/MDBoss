# MD Boss

A local Markdown manager, editor, and GitHub-style viewer. Browse Markdown
files across up to **five root folders**, edit them beside a **live preview**
rendered with the **GitHub-light** theme, **mermaid** diagrams, and embedded
images — all **100% offline**.

## Getting started

1. Click **Manage folders…** and add one to five root folders. Each root
   appears as a top-level node in the file tree.
2. Single-click a `.md` file to open it. The source appears on the left, the
   rendered preview on the right.
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
  Markdown files anywhere inside them are not listed. A **single click** on a
  file opens it. Filter with the box above the tree. Right-click a file for
  New file/folder, Rename, Delete (to the Recycle Bin), Reveal in Explorer,
  Copy path, and Favorite. Right-click a **top-level folder** and choose
  **Show as flat list** to list all of its Markdown files at once instead of a
  tree — useful for a deep folder structure with only a few documents; the
  choice is remembered per folder.
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

Documents opened this way appear in **Recent**, which is the easiest way back to a file that lives outside your root folders. Use **Add to favorites** to keep one for good.

**The title bar tells you when a document is not in your tree.** A file under one of your root folders shows just its name — the tree already says where it lives. A file from anywhere else shows its **full path** after the name, so a document opened by double-clicking in Explorer, dropped on the window, or handed over on the command line never leaves you wondering which copy you are looking at:

```text
BannerCheck.md - C:\Users\you\Downloads\BannerCheck.md - MD Boss - v1.2.4
```

**Saving a new document suggests a name.** A file that has never been saved has no filename to offer, but it usually has a title — the one you typed at the New-from-template prompt, or the first heading you wrote. The Save dialog opens with that title as the filename, ready to accept or edit. A `title:` in YAML front matter is used in preference to the heading, characters Windows forbids in a filename are dropped, and if the document has no title yet the box simply opens empty.

**Close document** (**Ctrl+W**, or the **×** on the toolbar) closes what is open without closing MD Boss: the editor and preview go back to empty and no file is open. Unsaved changes are offered for saving first, exactly as they are when you open something else. It is greyed out when there is nothing to close.

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
- **Hide YAML** — when on (the default), a leading YAML front-matter block (`--- … ---` at the very top of the file) is not shown in the preview. Also on the **View** menu, as **Ctrl+Y**.

**Every one of these is remembered.** Each toggle is written to your settings the moment you click it — not when MD Boss exits — so the layout you chose survives a crash, a forced shutdown or a power cut just as well as a normal close. Pane widths are still saved on exit, since they are only worth reading once you have stopped dragging them.

## Snippets

The **Snippets** menu drops a block in at the cursor.

The first five are GitHub *alerts* — callout boxes:

| Snippet | For |
|---------|-----|
| **Note** | Information worth taking in even when skimming |
| **Tip** | Optional advice for being more successful |
| **Important** | Information necessary to succeed |
| **Warning** | Critical content needing immediate attention |
| **Caution** | Negative consequences of an action |

Each inserts a marker line and a starting sentence, which you then replace:

```markdown
> [!NOTE]
> Highlights information that users should take into account, even when skimming.
```

Then three more:

- **Mermaid diagram** — a ` ```mermaid ` fence with a small left-to-right flowchart to edit. The preview draws it as a real diagram. Node labels are quoted (`A["Start"]`) because the parser needs them to be: an unquoted label containing `&`, `/` or parentheses fails to parse, and a diagram that fails to parse renders as nothing at all.
- **Table** — a three-column GFM table with its separator row and two empty rows.
- **Insert image file…** — asks for an image, then writes a Markdown reference to it. The dialog opens in the document's own folder, since figures usually live beside the document that shows them.

**Blank lines are added around every snippet** as needed, because each of these only renders as a block when it stands alone. An alert pasted into a paragraph comes out as literal `[!NOTE]` text, and a line typed straight after a table becomes another row — so a blank line goes in after the block too, with the cursor left on it.

### How image paths are written

The reference is made **relative to the document** whenever the two are on the same drive, because that is what keeps working when the pair is moved or committed together:

```markdown
![shot](img/shot.png)
```

An absolute path is used only when nothing else can work — the image is on a different drive, or the document has not been saved anywhere yet — and the status bar says so, because an absolute path breaks the moment the document is sent to anyone. Save the document beside the image and insert again to get a relative one.

Paths containing spaces or brackets are wrapped in `<>` (`![my shot](<my shot.png>)`), which is the standard Markdown form for that and stays readable. Separators are written as forward slashes: a Markdown link destination is a URL, where a backslash is an escape character.

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

Templates are plain `.md` files in `%APPDATA%\MDBoss\templates`; use **New file ▸ Manage templates…** to open the folder. Starters are written there for you, each one only the first time that version of MD Boss runs — a template you delete stays deleted. Templates may use these placeholders, filled in when the file is created:

| Placeholder | Becomes |
|-------------|---------|
| `{{title}}` | the new file's name (or "New document") |
| `{{date}}`  | today's date, `YYYY-MM-DD` |
| `{{time}}`  | the current time, `HH:MM` |
| `{{datetime}}` | date and time |
| `{{year}}`  | the current year, `YYYY` |

### The TechNote template

**TechNote** starts a tech note with the standard header: front matter, the banner logo, the `TN {{year}}-0X` number, the byline, and a **References** section.

Its banner logo needs no setting up. The template carries the image inline, so the banner renders the moment the document is created — before it has been saved anywhere. When you do save it, MD Boss writes `background-logo.png` into the same folder and points the document at it, leaving you the ordinary relative reference every hand-written tech note uses:

```markdown
<img src="background-logo.png" alt="image-20240901145033347" style="zoom: 50%;" /> TN 2026-0X
```

A `background-logo.png` already in that folder is never replaced — the copy your other notes point at is left alone.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N   | New file |
| Ctrl+S   | Save |
| Ctrl+W   | Close the open document |
| Ctrl+Y   | Hide/show YAML front matter |
| F5       | Refresh the tree |
| F1       | This help |

## Updates

MD Boss checks for updates when you ask it to, from **Help → Check for
updates**. When a newer version is available it offers to **download and
install it now**: an installed copy fetches the installer, quits, installs
silently, and restarts on the new version. Choose *No* to skip a version or
*Cancel* to close without updating.

A portable copy updates the same way, by copying the new build over its own
folder. If that copy fails for any reason the old version is left intact and
still runs, so a failed update never leaves you without a working app.

It also **reloads a document you have open when it changes on disk**, as long
as you have no unsaved edits. If you do have unsaved edits it keeps them and
says so in the status bar; it never overwrites your work.

## The old Python build

Earlier versions of MD Boss were a Python app that also ran on Linux. That
build is **no longer maintained or updated** — this Windows build (C++ /
wxWidgets; **About** shows the version) is the one that ships. If you used the
Python build, both read the same settings file, so your folders, favorites and
recents carry over automatically. You can uninstall the old *MD Boss* once
you have this one.

## Privacy

The preview's web view is **network-locked**: any remote image, script, or
link inside a document is blocked, so document content never leaves your
machine. The only network requests MD Boss makes are the optional update check
(and downloading an update if you accept one) against its own GitHub releases
page — never anything from your documents.
