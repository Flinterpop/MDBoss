# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this repo is

A Markdown manager/editor with an offline GitHub-style preview. Two
implementations live here, but they are no longer peers:

- **`MDBossCpp/`** — a C++20/wxWidgets app, Windows only. **This is the
  reference implementation and the only thing that ships** (installer +
  portable zip). When something is ambiguous, the C++ app defines the
  behaviour.
- **`app.py` + `mdrender.py`** — Python/PySide6. **Deprecated as of v1.2.2.**
  Kept in-tree as a historical reference and parity record, but it is no
  longer the oracle, is no longer built, gated, or shipped, and **Linux is no
  longer a supported target** (the AppImage is gone). Do not treat a
  difference from the Python app as a bug in the C++ app any more — if a
  change is wanted, it is made in `MDBossCpp/` and the Python side is left
  as-is. Touch `app.py`/`mdrender.py` only for an explicit request about the
  legacy app. Its packaging is **guarded off**, not just unused: `MDBoss.spec`
  exits unless `MDBOSS_BUILD_DEPRECATED_PYTHON` is set, and `installer.iss` /
  `build-appimage.sh` refuse without `/DAllowDeprecatedPythonBuild` /
  `MDBOSS_BUILD_DEPRECATED_PYTHON`. The guards exist because `MDBoss-Setup.exe`,
  `MDBoss-Portable-App.zip` and the AppImage are the asset names old installs
  poll for — regenerating one by accident and publishing it would silently
  "update" those installs back onto the dead app. Use the overrides only for
  local archaeology; never publish what they produce.

Both still read and write the **same** `%APPDATA%\MDBoss\config.json`, so a
user who ran the old Python app keeps their profile: *never drop a key you do
not understand.* Python stored window layout as base64 Qt blobs that are
meaningless to the port and unreconstructable if lost, so saving merges into
what is on disk rather than rewriting it, and the port keeps its own layout
under separate `wx_*` keys.

The repo is **public**. Everything here is world-readable the moment it is
pushed.

## Where the C++ app deliberately differs from the legacy Python app

These were the deliberate divergences from the Python app back when it was the
oracle. They are recorded here because the golden corpus and a few tests still
encode the old Python behaviour, so knowing which differences are intentional
still matters when reading a failing test. They are **not** things to "fix" by
making the C++ app match Python — the C++ app is the reference now.

1. **Highlighting.** Python highlighted server-side with Pygments; the C++ app
   does it in the browser with highlight.js. Fenced-block markup therefore
   differs, and the golden corpus compares that one case as an opaque unit.
2. **The document watcher.** The C++ app reloads the open document when it
   changes on disk (never discarding unsaved edits); Python rescanned on F5
   only.
3. **Update checks.** Python checked on launch; the C++ app only when asked.
4. **Portable-update extraction.** Python unpacked the update zip in-process
   (`zipfile`); the C++ app has no zip library, so its handoff batch extracts
   with `%SystemRoot%\System32\tar.exe` and gates the copy on finding
   `MDBoss.exe` in the result — same no-brick guarantee, different place. See
   `portable_batch` in `MDBossCpp/app/Updater.cpp`.
5. **Non-UTF-8 files on open.** Python failed with a decode error; the C++ app
   detects UTF-16/CP1252 and offers to convert (binary garbage is refused
   outright). This shipped in v1.2.1 after a save bug: v1.2.0's save could
   write a buffer whose first 16 bytes the heap had reclaimed, and the strict
   `FromUTF8` load path then loaded the damaged file as an *empty* editor —
   one Ctrl+S from wiping it. Saves are now validated and read back
   (`write_text_file_checked` in `FileScan.cpp`).

## Before you change anything

**Run the C++ suite:** `ctest --test-dir MDBossCpp/build -C Release`. This is
the gate now — the Python suite (`python -m pytest -q`) still works and can be
run by hand against the legacy app, but it no longer gates a release. Several
C++ tests exist because a specific bug shipped, and they are worth reading
before working near what they guard:

| Test | Exists because |
|---|---|
| `test_version.cpp` | a version bump reached 2 of 7 files and nothing noticed |
| `test_sources.cpp` | a PowerShell rewrite double-encoded every dash in a file; also bans non-ASCII in **narrow** string literals, which wx renders as mojibake |
| `test_updater.cpp` | the update handoff has failed silently three times |
| `test_fileassoc.cpp` | the registry plan matched the Python app's exactly; that layout is now the C++ app's own spec |
| `test_fileio.cpp` | a save wrote a partly-freed buffer and corrupted the first 16 bytes of six files; the validator + read-back guard against it |

The golden corpus under `MDBossCpp/tests/golden/` was generated from the
Python renderer (`make_golden.py`) back when it was the oracle. It is now a
**frozen expectation** for the C++ renderer, not a live parity check — a
deliberate rendering change means regenerating the affected golden file, not
matching Python.

## Releasing

`.\release.ps1 <version>` — and **only** that. It owns the version in seven
places (still seven: the deprecated `app.py` and `installer.iss` stay in the
lockstep so they never ship a wrong number if resurrected), builds the C++
installer and portable zip, runs the C++ `ctest` suite, commits, pushes and
publishes. Bumping by hand misses a file. Three asset names are **gone
deliberately** and must not reappear — the Windows Python assets
(`MDBoss-Setup.exe`, `MDBoss-Portable-App.zip`) and the Linux
`MDBoss-x86_64.AppImage`: an old install seeing its own asset name would
silently "update" itself with whatever that asset holds. There is no AppImage
and no Linux build any more (deprecated with the Python app in v1.2.2); the
Python suite no longer gates the release.

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
