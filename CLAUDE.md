# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this repo is

**Two applications, one version, one release.** A Markdown manager/editor with
an offline GitHub-style preview:

- **`app.py` + `mdrender.py`** — Python/PySide6. This is the **shipping**
  product, on Windows and Linux, and the **parity oracle**: where the two
  disagree, Python is right and the port is wrong.
- **`MDBossCpp/`** — a C++20/wxWidgets port, Windows only. At feature parity,
  but younger and with fewer miles on it.

Both read and write the **same** `%APPDATA%\MDBoss\config.json`. That imposes
a hard rule: *never drop a key you do not understand.* Python stores window
layout as base64 Qt blobs that are meaningless to the port and unreconstructable
if lost, so saving merges into what is on disk rather than rewriting it, and the
port keeps its own layout under separate `wx_*` keys.

The repo is **public**. Everything here is world-readable the moment it is
pushed.

## The three deliberate divergences

Anything else that differs is a bug. These are not:

1. **Highlighting.** Python highlights server-side with Pygments; the port does
   it in the browser with highlight.js. Fenced-block markup therefore differs,
   and the golden corpus compares that one case as an opaque unit.
2. **The document watcher.** The port reloads the open document when it changes
   on disk (never discarding unsaved edits); Python rescans on F5 only, which
   its `_refresh_watcher()` comment says was a deliberate choice.
3. **Update checks.** Python checks on launch; the port only when asked.

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
places, builds both Windows apps *and* the Linux AppImage (through WSL), runs
both suites, commits, pushes and publishes. Bumping by hand misses a file.

Two traps, both learned the hard way:

- **Do not pipe it through `2>&1 |`.** PowerShell 5.1 wraps a native command's
  stderr in ErrorRecords and `$ErrorActionPreference = "Stop"` turns
  PyInstaller's first INFO line into a terminating error. Run it bare.
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
