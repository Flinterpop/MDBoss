# MD Boss

[![Latest release](https://img.shields.io/github/v/release/Flinterpop/MDBoss?sort=semver)](https://github.com/Flinterpop/MDBoss/releases/latest)
[![Release date](https://img.shields.io/github/release-date/Flinterpop/MDBoss)](https://github.com/Flinterpop/MDBoss/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Flinterpop/MDBoss/total)](https://github.com/Flinterpop/MDBoss/releases)
[![License: MIT](https://img.shields.io/github/license/Flinterpop/MDBoss)](LICENSE)

A local Markdown manager, editor, and GitHub-style viewer for Windows.

MD Boss browses Markdown files across up to **five root folders**, edits them
in a source pane, and renders a **live preview** using the **GitHub-light**
theme with **mermaid** diagrams and embedded images. Rendering is **100%
offline** — mermaid, the GitHub stylesheet, and syntax highlighting are all
bundled — and the preview web view is **network-locked** so document content
can never reach the network.

It is a PySide6 sibling of [PDF Sherpa](https://github.com/Flinterpop/PDF_Sherpa)
and shares its conventions and release pipeline.

## Features

- Up to five root folders in one combined file tree, with filter and full
  right-click file management (new, rename, delete-to-Recycle-Bin, reveal).
- Source editor with line numbers beside a live GitHub-rendered preview.
- Mermaid diagrams, embedded local images, Pygments-highlighted code.
- Document outline that scrolls the preview; favorites (up to ten).
- Remembers your roots, window layout, and favorites between runs.

## Install

Download **MDBoss-Setup.exe** (per-user installer) or **MDBoss-Portable.zip**
(just the exe) from the
[Releases](https://github.com/Flinterpop/MDBoss/releases/latest) page.

## Run from source

```
pip install -r requirements.txt
python app.py
```

## Build a release

```
.\release.ps1 0.1.0
```

This bumps the version, builds the one-file exe (PyInstaller), the installer
(Inno Setup 6), and the portable zip, commits and pushes, then publishes a
GitHub release with both assets. Requires `python` (with PyInstaller), Inno
Setup 6, `gh` (authenticated), and `git`.

## License

MIT — see [LICENSE](LICENSE).
