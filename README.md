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
- Registers as a Windows Markdown handler (per-user, no admin) from the
  **File types…** dialog or an installer checkbox — Windows still requires you
  to pick the default yourself, which the dialog walks you through.
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
**MDBoss-Portable-App.zip** from the
[Releases](https://github.com/Flinterpop/MDBoss/releases/latest) page. The
portable zip holds an `MDBoss` folder — extract it somewhere and run
`MDBoss.exe` from inside it; the exe needs the `_internal` folder beside it.

> Upgrading a **portable** copy of v0.1.11 or earlier: download the new zip by
> hand. Those versions expect a single-exe zip and cannot install a one-dir
> build over themselves; the in-app updater will send you here instead.

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

This bumps the version **everywhere it appears** — seven files across both
apps, listed in the script — builds the app folder (PyInstaller, one-dir), the
installer (Inno Setup 6), the portable zip, the C++ installer and the Linux
AppImage, runs both test suites, commits and pushes, then publishes a GitHub
release with all five assets. Requires `python` (with PyInstaller), CMake with
a Visual Studio toolchain and vcpkg, Inno Setup 6, `gh` (authenticated), `git`,
and WSL for the AppImage. Installed copies then pick up the new release via the
in-app updater.

Bump the version by hand and you will miss a file; that is what the script and
the lockstep test exist to prevent.

The build is **one-dir**, not one-file: MD Boss can be the Windows handler for
`.md`, and a one-file build unpacks ~230 MB into `%TEMP%` on every launch —
about 3.8 s to a window, against 0.75 s for one-dir, measured on the dev
machine. The cost is a larger install on disk (~600 MB unpacked).

`./build-appimage.sh` produces `dist/MDBoss-x86_64.AppImage` and its companion
`dist/MDBoss-x86_64.AppImage.zsync`. `release.ps1` runs it through WSL from the
same working tree and publishes both, so a release cannot ship without them by
accident — `-SkipAppImage` is the only way to leave them out, and it says so
loudly. **Both** matter: an installed AppImage reads the `.zsync` to find its
update, so shipping one without the other breaks self-update as completely as
shipping neither.

## The C++ port

`MDBossCpp/` holds a C++ / wxWidgets port of the same application, Windows
only. **The Python app above is the one that ships**, and it is the reference
implementation: where the two disagree, Python is right and the port is wrong.

The port exists for toolchain consistency with the other C++ projects it sits
beside, and for size — the installer is about 5 MB against roughly 157 MB of
packaged Python, and it needs no VC++ redistributable. Start-up is not the
reason: both embed Chromium to render, so both are dominated by that.

It reads and writes the **same** `%APPDATA%\MDBoss\config.json`, keeping its
own window layout under separate `wx_*` keys, so you can run either against
one profile without them fighting.

It installs separately (as *MD Boss (C++)*) and updates itself the same way the
Python build does — `Help → Check for updates` downloads its own installer,
waits for the app to close, installs and reopens.

Three differences are deliberate rather than unfinished:

- Code is highlighted in the browser by **highlight.js** rather than
  server-side by Pygments, so a fenced block's markup differs.
- The port **reloads the open document when it changes on disk**, keeping your
  caret and never discarding unsaved edits; the Python app rescans only on F5.
- The port **never checks for updates on launch**, only when asked. The Python
  app checks at start-up.

```
cmake -S MDBossCpp -B MDBossCpp/build
cmake --build MDBossCpp/build --config Release
ctest --test-dir MDBossCpp/build -C Release
```

Needs vcpkg at `C:\vcpkg` (triplet `x64-windows-static`), installed **without**
the `webview` feature — the preview is a WebView2 this app creates and owns,
because `wxWebView`'s Edge backend never delivers `WebResourceRequested` and so
cannot enforce the network lock.

The renderer is tested against a golden corpus generated from the Python
renderer, so parity is checked rather than assumed. `CLAUDE.md` records the
conventions and the traps that produced them.

## License

MIT — see [LICENSE](LICENSE).
