# MD Boss

[![Latest release](https://img.shields.io/github/v/release/Flinterpop/MDBoss?sort=semver)](https://github.com/Flinterpop/MDBoss/releases/latest)
[![Release date](https://img.shields.io/github/release-date/Flinterpop/MDBoss)](https://github.com/Flinterpop/MDBoss/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Flinterpop/MDBoss/total)](https://github.com/Flinterpop/MDBoss/releases)
[![License: MIT](https://img.shields.io/github/license/Flinterpop/MDBoss)](LICENSE)

A local Markdown manager, editor, and GitHub-style viewer for Windows and Linux.

MD Boss browses Markdown files across up to **five root folders**, edits them
in a source pane, and renders a **live preview** using the **GitHub-light**
theme. Rendering is **100% offline** — mermaid, KaTeX, the GitHub stylesheet,
and syntax highlighting are all bundled — and the preview web view is
**network-locked** so document content can never reach the network.

It is a PySide6 sibling of [PDF Sherpa](https://github.com/Flinterpop/PDF_Sherpa)
and shares its conventions and release pipeline.

## Features

**Browse & manage**
- Up to five root folders in one combined file tree; each folder shows its
  recursive Markdown-file count, and folders holding no Markdown are hidden.
  Filter box, and full right-click file management (new from template, new
  folder, rename, delete (to the Recycle Bin / Trash), reveal in your file
  manager, copy path).
- Recent documents at the top of the left pane: the last six you opened,
  newest-first, maintained automatically.
- Favorites below Recent (both resizable): newest-first, up to ten, with a
  right-click menu and export / import / clear. Importing a favorites file
  exported on Windows remaps its drive-letter paths (e.g. `J:\Dropbox\…`) onto
  your home folder on Linux.
- New-file templates (`%APPDATA%\MDBoss\templates` on Windows,
  `~/.config/MDBoss/templates` on Linux; `{{title}}`/`{{date}}` placeholders).
- Works as a plain Markdown viewer too: **Ctrl+O**, drag-and-drop, or a path on
  the command line opens any file on disk, root folder or not. A second launch
  hands its document to the running window instead of starting another copy.
- `MD_Inbox`: create a folder of that name in a root and use **Import files
  into MD_Inbox…** to copy documents in, collision-safe.

**Edit & preview**
- Source editor with line numbers beside a live GitHub-rendered preview,
  synced scrolling in both directions, and a document outline that jumps the
  preview to a heading.
- Optional hiding of a leading YAML front-matter block; full file path in the
  title bar.

**Rendering** (all offline)
- Mermaid diagrams, embedded images, raw HTML embeds (sanitized).
- GitHub alerts (`> [!NOTE]` …) and MkDocs/Material admonitions
  (`!!!` / `???` / `???+`).
- LaTeX math (`$…$`, `$$…$$`) via KaTeX; Pygments-highlighted code.

**Distribution**
- Windows per-user installer + portable zip, and a self-contained Linux
  **AppImage**. **In-app auto-update** downloads and installs new releases in
  place on both platforms (the AppImage replaces itself and relaunches).
- Remembers your roots, window layout, recent documents, favorites, and
  preferences.

## Install

**Windows** — download **MDBoss-Setup.exe** (per-user installer) or
**MDBoss-Portable.zip** (just the exe) from the
[Releases](https://github.com/Flinterpop/MDBoss/releases/latest) page.

**Linux** — download **MDBoss-x86_64.AppImage** from the same page, then:

```
chmod +x MDBoss-x86_64.AppImage
./MDBoss-x86_64.AppImage
```

The AppImage is self-contained (bundled Python + Qt), so nothing else is
needed. It runs the renderer with `QTWEBENGINE_DISABLE_SANDBOX=1` because an
AppImage cannot ship the setuid chrome-sandbox helper.

It **updates itself**: on launch it checks the GitHub releases and, when a
newer version is available, offers to download it and replace the running
AppImage in place, then relaunch. Update information is embedded and a
companion `.zsync` is published, so external tools (AppImageUpdate,
appimaged) can update it too.

## Run from source

```
pip install -r requirements.txt
python app.py
```

On Linux/macOS you can instead use **`./run.sh`**, which creates a local
`.venv`, installs the requirements on first run, and launches the app;
**`./install-linux.sh`** adds a menu entry, and **`./build-appimage.sh`**
builds the AppImage.

## Build a release

```
.\release.ps1 <version>       # e.g. .\release.ps1 0.1.11
```

This bumps the version, builds the one-file exe (PyInstaller), the installer
(Inno Setup 6), and the portable zip, commits and pushes, then publishes a
GitHub release with both assets. Requires `python` (with PyInstaller), Inno
Setup 6, `gh` (authenticated), and `git`. Installed copies then pick up the
new release via the in-app updater.

On Linux, `./build-appimage.sh` produces `dist/MDBoss-x86_64.AppImage` and its
companion `dist/MDBoss-x86_64.AppImage.zsync`; upload both to the release
alongside the Windows assets. Existing AppImages then self-update to it.

## License

MIT — see [LICENSE](LICENSE).
