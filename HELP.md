# MD Boss

*Last updated: 16 Aug 2026*

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

- **Left pane** — **Recent** and **Favorites** sit at the top (see below), above the **Files** list: your roots as a combined tree, where each folder shows the number of Markdown files within it (counted recursively). Folders with no Markdown files anywhere inside them are not listed. A **single click** on a file opens it. Filter with the box above the tree — see **Searching** below. **The folders you leave open are remembered**, so the tree comes back the way you left it next time you start. Right-click a file for New file/folder, Rename, Delete (to the Recycle Bin), Reveal in Explorer, Copy path, and Favorite. Right-click **any folder** — a root or a subfolder at any depth — and choose **Show as flat list** to list every Markdown file beneath it at once instead of a tree. Useful for a deep structure holding only a few documents. A flattened folder is marked **`(flat)`** after its count, since a folder showing no subfolders otherwise looks the same as one that has none. Each folder is independent: one subfolder can be flat while its siblings and its root stay trees, and the count beside a flattened folder is exactly how many files it lists. The choice is remembered per folder. The same menu offers **Skip when scanning** for a folder that should be left out of the scan altogether — see **Big root folders** below.
- **Outline** (middle) — headings of the current document. Click a heading to
  scroll the preview to it.
- **Recent** (top of the left pane) — the last six documents you opened, newest
  first, kept automatically. Re-opening one moves it back to the top rather than
  adding a duplicate. **A single click opens it**, the same as the files tree;
  right-click to open, favorite, reveal, or
  copy a path, and use the **⋯** menu to clear the list.
- **Favorites** (below Recent) — pin up to ten documents, newest first
  (adding an 11th drops the oldest). Drag the divider below it to make the
  panel taller or shorter. Rows show the filename (hover for the full path);
  missing files show in red. **A single click opens one.** Right-click a file in the tree to add or remove
  it; right-click a favorite to open, remove, reveal, or copy its path; and use
  the **⋯** menu in the Favorites header to export, import, or clear the list.
- **Editor \| Preview** (right) — the source editor beside the live preview. The **Edit** and **Preview** toolbar buttons hide either one: turn **Edit** off for distraction-free reading, or **Preview** off to give the editor the whole pane. They share one splitter, so they cannot both be off — turning the second one off brings the pair back. Both choices are remembered.

## Big root folders

Adding a whole working folder as a root is a reasonable thing to want, and it works — but the scan walks everything beneath it, and generated folders can hold far more than your documents do. A build tree, a package cache or an application's own output directory can easily contain hundreds of times as many files as the notes you actually want listed, and every one of them costs the scan time.

**Right-click any folder inside a root and choose *Skip when scanning*** to leave it out. The choice is remembered, and refreshing afterwards is enough to see the difference. A skipped folder is still listed, marked **`(excluded)`** with a count of `0` — that row is how you find it again, and right-clicking it a second time puts it back.

Two things worth knowing while a scan is running:

- **It runs in the background.** The root folders appear immediately and the window stays usable; the folder counts and the documents beneath them fill in when the walk finishes. On a very large root that can take a while, and nothing is wrong.
- **A root marked `(partial — scan limit reached)` is not fully listed.** MD Boss stops walking a single root once it has seen a very large number of files, so a runaway folder cannot keep it busy forever. Real folders do not reach that limit, so seeing it means something enormous is inside — find the folder responsible, skip it, and press **F5**.

Press **F5** at any time to rescan and pick up files added or removed outside MD Boss.

## Moving files by dragging

**Drag a document onto a folder in the files tree to move it there on disk.** Dropping it onto another *file* puts it beside that file, in the folder it lives in — usually easier to aim at than the folder row itself.

Folders cannot be dragged. Only documents move.

**Most moves happen without asking**, because a tree you also drag to scroll is a poor place to be interrogated. You are asked in exactly two cases:

- **The name is already taken** in the destination — you are asked whether to *replace* the file there, and the safe answer is the default. Replacing a document cannot be undone.
- **You are moving between two different top-level folders** — nothing is destroyed, but the document leaves the folder it was filed under, which is worth a moment's confirmation.

A move keeps everything pointing at the file: it stays in **Favorites** and **Recent**, and if it is the document you have open, MD Boss follows it — so **Ctrl+S** still saves to the right place. The same is now true when you **Rename** a file, which previously left the open document pointing at its old name.

There is **no undo**. If you move something by accident, drag it back.

## Links in the preview

**Click a link and it opens in your browser.** `http`, `https` and `mailto:` links all work; the preview itself stays where it is, showing the document you were reading.

The link is handed to Windows to open, which is why it leaves MD Boss entirely. The preview never loads anything from the network itself — a remote image or script in a document is still blocked, exactly as before. Following a link is a click you made; fetching a tracking pixel is not, and that distinction is the whole point.

A link to anything other than those three kinds is ignored rather than opened.

## Preview style

**View ▸ Preview style** offers two looks, and the choice is remembered:

- **GitHub** — the default: GitHub's own Markdown stylesheet, ruled headings, GitHub's syntax colours. What a document looks like on GitHub.
- **Notes** — quieter and more page-like: Segoe UI, headings that step up only slightly and carry no rules, tighter line spacing, code in a rounded bordered panel with an Xcode-style palette, and a narrower column.

Only the styling changes — the document is untouched, and switching re-renders in place without disturbing your scroll position or any unsaved edit. **Export as PDF uses whichever style is showing**, so pick the style first if you are exporting.

## Export as PDF

**File ▸ Export as PDF…** writes the document exactly as the preview shows it — the GitHub styling, the code-block shading and syntax colours, mermaid diagrams, KaTeX maths, tables and task-list boxes. The filename is suggested from the document's own name, in its own folder.

**Hyperlinks stay blue and stay clickable.** They are written as real PDF link annotations, so clicking one in Acrobat, Edge or any other reader opens the URL in your browser.

The page is printed without the header and footer a browser would normally add, so there is no URL or date stamped across the top of every page. What you get is the page, not a printout of a web page.

Exporting needs the preview to be up; if you try during the first second or two after launch it will say so rather than produce a blank file.

## Lists: logins, to-dos, and the Grail Diary

The **Lists** menu keeps three standing lists for you. Each command opens a small form, and what you type is appended to a Markdown file in a folder called **`MD_Internal`**, which sits beside your `MD_Inbox`. New entries always go at the end.

| Command | Writes to | What it adds |
|---|---|---|
| **Add a login record…** | `logins.md` | A row in a table: Name, Link, Login, PW, Last Changed, Notes |
| **Add a to-do…** (`Ctrl+T`) | `ToDoList.md` | A tickable checklist item, dated |
| **Add a Grail Diary entry…** | `GrailDiary.md` | Your Markdown, under a dated heading |

### The tech-note index

**Lists ▸ Rebuild the tech-note index…** finds every tech note under your folders and writes `MD_Internal\TechNotes.md` — a table of TN index, title, version, subject, GUID and file — then opens it, in number order with any unnumbered notes at the end.

**A document counts as a tech note only if its front matter has both a `GUID:` and `TechNote` among its `keywords:`.** Both are required: the keyword says what the document is, the GUID says which one. A stray GUID in some other document will not put it on the list, and deleting the keyword takes a note off it. The TechNote template writes both for you, and fills the GUID in per note.

### Tech-note numbers

A new tech note is numbered **`TNIndex: 2026.04`** — the year, then a sequence that restarts each January. The number is filled in when you create the note, and it is worked out the same way the index is: MD Boss reads the notes you already have and takes **one past the highest** number that year. A note deleted from the middle of a year does not have its number handed out again, since something else may still cite it.

Two things follow from deriving it rather than keeping a counter:

- **Notes made before this existed have no number, and are left alone.** They still appear in the index, after the numbered ones. Give one a number by typing it into its front matter; the next new note counts it from then on.
- **A number claimed by two notes is reported, not silently fixed.** The rebuild is the only thing that ever sees every note at once, so it is the only thing that can notice — it prints a **Duplicate numbers** line under the table. Which of the two should move is your call.

Your own template needs only the key: a line reading `TNIndex:` with nothing after it is filled in on use. A number already written in is left as it is.

**The index is rebuilt, not accumulated.** It is derived from the documents each time you run the command, so notes you created before this feature existed, or made in another editor, or renamed, moved or deleted, all come out right. That also means it is the one file in `MD_Internal` you should **not** edit — your changes are lost on the next rebuild. It carries the date it was made, so a stale one says so.

Rebuilding reads the beginning of every Markdown file under your folders, which is why it is a command rather than something that happens on every refresh.

**Dates are written as day, month, year** — `16 Aug 2026`. The to-do's date and the diary's heading are filled in for you; the login record's *Last Changed* is filled in too but stays editable, since the password may have been changed on some other day.

**Use of MD_Internal:** the folder and its files are created the first time you add something, and `MD_Internal` shows in the files tree like any other folder — its documents are counted, filtered and searched exactly as the rest of yours are. Nothing in it is hidden or special-cased. **Open MD_Internal folder** on the same menu takes you there in Explorer.

**These are ordinary Markdown files.** Edit them by hand whenever you like — tick a box, reword an item, sort the table, delete a row. MD Boss only ever appends; it never rewrites what is already there, and it will not add a second heading or table header to a file that has one.

If you have no `MD_Inbox`, `MD_Internal` is created in your first folder instead, so the commands work without setting anything up.

### A warning about logins.md

**Passwords are stored as plain text.** Anyone who can read the file can read them, and because it is an ordinary document its contents are indexed by the **Contents** search — a password can appear in a search result.

To limit the damage, MD Boss writes a `.gitignore` into `MD_Internal` when it creates the folder, so the files cannot be committed to a git repository by accident. Do not delete that file unless you have thought about it, and do not keep anything in `MD_Internal` you would mind being read by whoever can read the folder.

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

## Searching

The box above the tree filters by **filename** as you type. **Filtering keeps the folder structure**: folders left with no surviving file drop out, the rest stay where they are, and the tree opens itself down to every match — so a result tells you where the document lives, not just what it is called. Clearing the box puts the tree back exactly as you had it.

Tick **Contents** beside it to search the **text inside files** as well, and the tree shows both — files whose name matches and files whose text does, each text match with the line number and the line itself so you can see why it matched without opening it:

```text
Notes  (412)
   guides  (18)
      install-guide.md
   release-notes.md
      12: install the service before first run
```

Here `install-guide.md` matched on its **name** and stays in the folder it lives in, while `release-notes.md` matched only on its **text** and is listed against the root with the matching line beneath it.

Clicking either the file or the matching line opens the file.

Only the **first** match in each file is shown — the point is to find the right document, not to replace a grep. Matching is case-insensitive.

A few limits, all of them there because a content search reads every Markdown file under every root:

- It needs at least **two characters**. One letter matches almost everything and costs the most to find out.
- It runs **in the background**, starting once you stop typing for a moment, so the window never freezes while it works. The tree shows *searching…* until results arrive, and a search you type past is abandoned rather than finishing into a stale list.
- Files over **1 MB** are skipped, and the search stops after 20,000 files or 500 matches.

Untick **Contents** to go back to filtering by name alone. The setting is not remembered between runs; the box always starts as a plain filename filter.

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
| `{{guid}}`  | a fresh unique identifier, different for every document |
| `{{tnindex}}` | the next tech-note number for this year, `2026.04` — see [Tech-note numbers](#tech-note-numbers) |

### The TechNote template

**TechNote** starts a tech note with the standard header: front matter, the banner logo, the `TN {{year}}-0X` number, the byline, and a **References** section. Its front matter carries `title`, `author`, `version`, `creator`, `subject`, `GUID`, `TNIndex` and `keywords`, and **the GUID and the TN index are filled in for you** — a new GUID for every note, so two created in the same minute can still be told apart, and the next number for the year.

**Changing the starter does not change a template you already have.** Starters are written by name and never over an existing file, which is what protects a `TechNote.md` you have edited — if yours carries your own banner line, MD Boss will not overwrite it. To take up a newer starter, edit your copy, or delete it so the starter is offered again on the next launch.

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
| Ctrl+T   | Add a to-do |
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
