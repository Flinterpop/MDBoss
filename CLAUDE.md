# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this repo is

**Two applications, one version, one release.** A Markdown manager/editor with
an offline GitHub-style preview:

- **`app.py` + `mdrender.py`** — Python/PySide6. The **reference
  implementation and parity oracle**: where the two disagree, Python is right
  and the port is wrong. It ships **only on Linux** (as the AppImage); its
  Windows installer and portable zip are no longer released.
- **`MDBossCpp/`** — a C++20/wxWidgets port, Windows only, and **the app that
  ships on Windows** (installer + portable zip).

Both read and write the **same** `%APPDATA%\MDBoss\config.json`. That imposes
a hard rule: *never drop a key you do not understand.* Python stores window
layout as base64 Qt blobs that are meaningless to the port and unreconstructable
if lost, so saving merges into what is on disk rather than rewriting it, and the
port keeps its own layout under separate `wx_*` keys.

The repo is **public**. Everything here is world-readable the moment it is
pushed.

## The five deliberate divergences

Anything else that differs is a bug. These are not:

1. **Highlighting.** Python highlights server-side with Pygments; the port does
   it in the browser with highlight.js. Fenced-block markup therefore differs,
   and the golden corpus compares that one case as an opaque unit.
2. **The document watcher.** The port reloads the open document when it changes
   on disk (never discarding unsaved edits); Python rescans on F5 only, which
   its `_refresh_watcher()` comment says was a deliberate choice.
3. **Update checks.** Python checks on launch; the port only when asked.
4. **Portable-update extraction.** Python unpacks the update zip in-process
   (`zipfile`) and validates it before closing; the port has no zip library,
   so its handoff batch extracts with `%SystemRoot%\System32\tar.exe` and
   gates the copy on finding `MDBoss.exe` in the result — same no-brick
   guarantee, different place. See `portable_batch` in
   `MDBossCpp/app/Updater.cpp`.
5. **Non-UTF-8 files on open.** Python fails with a decode error; the port
   detects UTF-16/CP1252 and offers to convert (binary garbage is refused
   outright). Both exist because of the same shipped bug: v1.2.0's save
   could write a buffer whose first 16 bytes the heap had reclaimed, and
   the port's strict `FromUTF8` then loaded the damaged file as an *empty*
   editor — one Ctrl+S from wiping it. Saves are now validated and
   read back (`write_text_file_checked` in `FileScan.cpp`).

## Before you change anything

**Run both suites.** `python -m pytest -q` and `ctest --test-dir MDBossCpp/build
-C Release`. Several tests exist because a specific bug shipped, and they are
worth reading before working near what they guard:

| Test | Exists because |
|---|---|
| `test_version.cpp` | a version bump reached 2 of 7 files and nothing noticed |
| `test_sources.cpp` | a PowerShell rewrite double-encoded every dash in a file; also bans non-ASCII in **narrow** string literals, which wx renders as mojibake |
| `test_updater.cpp` | the update handoff has failed silently three times across both apps |
| `test_fileassoc.cpp` | the registry plan must match Python's exactly |
| `test_app.py::test_send2trash_is_importable` | Send2Trash was declared but not installed, so Delete permanently destroyed files instead of using the Recycle Bin |

## Releasing

`.\release.ps1 <version>` — and **only** that. It owns the version in seven
places, builds the C++ installer and portable zip *and* the Linux AppImage
(through WSL), runs both suites, commits, pushes and publishes. Bumping by
hand misses a file. The Windows Python assets (`MDBoss-Setup.exe`,
`MDBoss-Portable-App.zip`) are **gone deliberately** and must not reappear:
an old Python install seeing its asset name would silently "update" itself
with whatever that asset holds.

Two traps, both learned the hard way:

- **Do not pipe it through `2>&1 |`.** PowerShell 5.1 wraps a native command's
  stderr in ErrorRecords and `$ErrorActionPreference = "Stop"` turns a
  build tool's first stderr line into a terminating error. Run it bare.
- **Test the update path against a real release**, by installing the previous
  build and letting it update itself. Unit tests pass while the handoff is
  broken; that is exactly how v1.1.2 shipped unable to update.

## Editing files from PowerShell: don't

`Get-Content | Set-Content -Encoding utf8` round-trips UTF-8 through the ANSI
code page and mangles every non-ASCII character, and adds a BOM. Use the Read
and Edit tools, or `[IO.File]::ReadAllText`/`WriteAllText`. `test_sources.cpp`
now catches it, but only for `app/` and `assets/`.

To repair damage already done: read the bytes as UTF-8, re-encode as CP1252,
write them back — that reverses the double-encoding exactly.

## Conventions

- **UTF-8 everywhere in the port.** `std::filesystem::path::string()` throws on
  unmappable characters, and constructing a `path` from a narrow string
  interprets it as ANSI. Use `PathUtf8.h`. Non-ASCII UI text must be a **wide**
  literal.
- **Bounded loops, checked returns, assertions** — NASA Power of 10, per the
  global instructions. First-party C++ builds `/W4 /WX`.
- **The preview is network-locked** and must stay that way: no remote image,
  script or link inside a document may load. The port owns its WebView2
  directly because `wxWebView`'s Edge backend never delivers
  `WebResourceRequested` and so cannot enforce it — see `app/PreviewPane.h`.
- Vendored asset versions are recorded in `assets/VERSIONS.md`; update it in
  the same commit that replaces an asset.

## ITAR

Everything under `C:\source` is export-controlled technical data, and the rules
in the global `~/.claude/CLAUDE.md` apply — but note this repo is **public**, so
repo permissions are not a backstop. Decide what may be committed *before* the
commit, not before the push.
