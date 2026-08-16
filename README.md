# MD Boss

[![Latest release](https://img.shields.io/github/v/release/Flinterpop/MDBoss?sort=semver)](https://github.com/Flinterpop/MDBoss/releases/latest)
[![Release date](https://img.shields.io/github/release-date/Flinterpop/MDBoss)](https://github.com/Flinterpop/MDBoss/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Flinterpop/MDBoss/total)](https://github.com/Flinterpop/MDBoss/releases)
[![License: MIT](https://img.shields.io/github/license/Flinterpop/MDBoss)](LICENSE)

*Last updated: 15 Aug 2026*

A local Markdown manager, editor, and GitHub-style viewer for Windows.

MD Boss browses Markdown files across up to **five root folders**, edits them
in a source pane, and renders a **live preview** using the **GitHub-light**
theme. Rendering is **100% offline** — mermaid, KaTeX, the GitHub stylesheet,
and syntax highlighting are all bundled — and the preview web view is
**network-locked** so document content can never reach the network.

It ships as a native **C++ / wxWidgets** build. (A Python/PySide6 version of
the same app lives in the repo but is **deprecated** — see
[The Python app (deprecated)](#the-python-app-deprecated) below.)

## Screenshot
<img width="2510" height="1628" alt="image" src="https://github.com/user-attachments/assets/720d7dd5-5ae2-474c-8fe0-a85dc809b7cd" />


## Features

**Browse & manage**
- Up to five root folders in one combined file tree; each folder shows its recursive Markdown-file count, and folders holding no Markdown are hidden. **A single click opens a file.** The folders you leave open are remembered between runs. Filter box, and full right-click file management (new from template, new folder, rename, delete (to the Recycle Bin), reveal in File Explorer, copy path).
- **Flat-list view:** right-click any folder — a root or a subfolder at any depth — and choose **Show as flat list** to see every Markdown file beneath it in one list instead of a folder tree — handy for a deep structure holding only a few documents. A flattened folder is marked `(flat)` after its count. Each folder is independent, so one subfolder can be flat while its siblings stay trees. The choice is remembered per folder.
- **Filtering keeps the tree:** typing in the filter box prunes the tree rather than flattening it — folders with no surviving file drop out, the rest keep their place, and the tree opens itself down to every match, so a result shows you where the document lives. Clearing the box restores the shape you had.
- **Search inside files:** the filter box matches filenames as you type; tick **Contents** beside it to search the text inside every Markdown file under your roots as well. Each text match is listed with its line number and the matching line. Runs in the background so the window never freezes, and a search you type past is abandoned rather than finishing into a stale list.
- Recent documents at the top of the left pane: the last six you opened,
  newest-first, maintained automatically.
- Favorites below Recent (both resizable): newest-first, up to ten, with a
  right-click menu and export / import / clear.
- New-file templates (`%APPDATA%\MDBoss\templates`; `{{title}}`/`{{date}}`/`{{year}}` placeholders), including a **TechNote** starter that carries its banner logo inline so it renders before the document is saved, then drops `background-logo.png` beside the file and switches to the ordinary relative reference on the first save.
- Works as a plain Markdown viewer too: **Ctrl+O**, drag-and-drop, or a path on
  the command line opens any file on disk, root folder or not. A second launch
  hands its document to the running window instead of starting another copy.
  A document from outside your root folders shows its **full path** in the title
  bar, so you always know which copy is open, and **Ctrl+W** (or the toolbar
  **×**) closes it again without closing the window.
- **Snippets menu:** the five GitHub alert callouts (Note, Tip, Important,
  Warning, Caution), a Mermaid diagram fence, a GFM table, and **Insert image
  file…** which browses for an image and writes a reference relative to the
  document. Every snippet is spaced so it renders as a block rather than as
  literal text, and the preview renders them the way GitHub does.
- Saving a never-saved document suggests a filename from its title (YAML
  `title:`, else the first heading), sanitised for Windows.
- Pane visibility and the Hide-YAML choice are written the moment you change
  them, so the layout survives however the app ends.
- Registers as a Windows Markdown handler (per-user, no admin) from the
  **File types…** dialog or an installer checkbox — Windows still requires you
  to pick the default yourself, which the dialog walks you through.
- `MD_Inbox`: create a folder of that name in a root and use **Import files
  into MD_Inbox…** to copy documents in, collision-safe.

**Edit & preview**
- Source editor with line numbers beside a live GitHub-rendered preview,
  synced scrolling in both directions, and a document outline that jumps the
  preview to a heading.
- Optional hiding of a leading YAML front-matter block; the document name and
  app version show in the title bar.

**Rendering** (all offline)
- Mermaid diagrams, embedded images, raw HTML embeds (sanitized).
- GitHub alerts (`> [!NOTE]` …) and MkDocs/Material admonitions
  (`!!!` / `???` / `???+`).
- LaTeX math (`$…$`, `$$…$$`) via KaTeX; syntax-highlighted code
  (highlight.js).

**Distribution**
- Windows installer + portable zip. **In-app auto-update** downloads and
  installs new releases in place.
- Remembers your roots, window layout, recent documents, favorites, and
  preferences.

## Install

Download **MDBoss-Cpp-Setup.exe** or **MDBoss-Cpp-Portable.zip** from the
[Releases](https://github.com/Flinterpop/MDBoss/releases/latest) page. The
installer asks whether to install for all users (Program Files, the default)
or just for you. The portable zip holds an `MDBoss` folder — extract it
somewhere and run `MDBoss.exe` from inside it.

It updates itself from `Help → Check for updates`: an installed copy
downloads `MDBoss-Cpp-Setup.exe`, a portable copy (no uninstaller beside the
exe) downloads `MDBoss-Cpp-Portable.zip` and swaps itself in place; either
way it waits for the app to close, updates, and reopens.

> **Running an old Python build (Windows or the Linux AppImage)?** It is no
> longer maintained, and releases no longer carry `MDBoss-Setup.exe`,
> `MDBoss-Portable-App.zip` or `MDBoss-x86_64.AppImage`, so it will not
> update itself past v1.2.1 — install `MDBoss-Cpp-Setup.exe` from here
> instead, then uninstall *MD Boss* (the Python one) if you no longer want
> it. Both read the same settings file, so your roots, favorites and recents
> carry over. Linux is no longer supported.

## Build from source

```
cmake -S MDBossCpp -B MDBossCpp/build
cmake --build MDBossCpp/build --config Release
ctest --test-dir MDBossCpp/build -C Release
```

Needs vcpkg at `C:\vcpkg` (triplet `x64-windows-static`), installed **without**
the `webview` feature — the preview is a WebView2 this app creates and owns,
because `wxWebView`'s Edge backend never delivers `WebResourceRequested` and so
cannot enforce the network lock. The installer is about 5 MB and needs no VC++
redistributable.

It reads and writes `%APPDATA%\MDBoss\config.json`, keeping its window layout
under `wx_*` keys, so it shares one profile with the old Python app without
the two fighting over settings.

## Build a release

```
.\release.ps1 <version>       # e.g. .\release.ps1 1.2.2
```

This bumps the version **everywhere it appears** (seven files, listed in the
script), builds the installer and portable zip (Inno Setup 6), runs the
`ctest` suite, commits and pushes, then publishes a GitHub release with both
assets. Requires CMake with a Visual Studio toolchain and vcpkg, Inno Setup 6,
`gh` (authenticated) and `git`. Installed copies then pick up the new release
via the in-app updater.

Bump the version by hand and you will miss a file; that is what the script and
the lockstep test exist to prevent.

## The Python app (deprecated)

`app.py` + `mdrender.py` are a Python/PySide6 build of the same application.
It was the original implementation and the reference the C++ app was ported
against; **as of v1.2.2 it is deprecated.** It is kept in the repo for
reference only — it is no longer built, released, or supported, and the Linux
AppImage that was built from it is discontinued. The C++ app in `MDBossCpp/`
is now the reference implementation.

You can still run it from source:

```
pip install -r requirements.txt
python app.py
```

`CLAUDE.md` records the conventions and the traps that produced them, and
which behaviours of the C++ app deliberately differ from this legacy one.

## License

MIT — see [LICENSE](LICENSE).
