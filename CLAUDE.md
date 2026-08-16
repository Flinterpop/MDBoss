# CLAUDE.md

*Last updated: 16 Aug 2026*

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

## Anything that reads files goes on a worker thread

Counting Markdown files once ran on the UI thread in `set_roots()`, and three real roots were enough to stall the message loop past the point where WebView2's asynchronous controller creation gives up: `CreateCoreWebView2Controller` returned `E_ABORT` and **the preview silently never appeared**. There is no error to see — the pane is just blank.

This used to be the one place the sibling app was the wrong model to copy: `PDF_Sherpa`'s `PdfListPane` built its entry list on the UI thread and got away with it, because nothing in that window waits on the message loop the way a WebView2 controller does. That is no longer true — its scan, its topic-list builds and its drop handling all run on workers now — so the two apps agree again, and the rule is simply that neither reads the filesystem in bulk on the UI thread.

So `start_scan()` (documents and counts) and `start_content_search()` (the Contents search) both follow the same shape, and anything new that touches the filesystem in bulk must too:

- a detached worker thread, never the UI thread;
- a **generation counter** bumped on each start, so a superseded result is
  discarded rather than applied out of order;
- `alive_`, a `shared_ptr<atomic<bool>>` the destructor clears, so a completion
  lambda can tell the panel is gone;
- the exception guard **inside** the per-root loop, not around it — wrapping
  the loop once meant a briefly-unavailable network folder discarded every
  other root's results and every tree read `(0)`;
- hard bounds on the work: `search_file_contents()` caps bytes per file, files
  walked, and matches returned, and uses an explicit worklist rather than
  recursion so a junction loop cannot hang the thread.

Note the difference between the two generation counters: `scan_generation_` is
read only on the UI thread inside `CallAfter`, so a plain `unsigned` is fine;
`search_generation_` is polled **by the worker** mid-walk so it can abandon a
search the user has typed past, which makes it `std::atomic<unsigned>`. Copying
the scan's declaration would have been a data race.

The golden corpus under `MDBossCpp/tests/golden/` was generated from the
Python renderer (`make_golden.py`) back when it was the oracle. It is now a
**frozen expectation** for the C++ renderer, not a live parity check — a
deliberate rendering change means regenerating the affected golden file, not
matching Python.

## The files tree is a view of one scan, not a view of the disk

`scan_root()` returns every Markdown file beneath a root *and* the per-folder counts from a single walk; `FileTreePanel` holds that list and builds every row from it. Nothing in the tree reads a directory — expanding a folder is free, and a filter prunes the structure instead of replacing it with a flat list of names, which is what the lazy design could not do. A folder node exists only because a document put it there, so a folder the walk could not read does not appear at all: there is no second listing to fall back on.

Two traps, both found only by driving the built app — neither shows up in a test:

- **Expansion restored at startup has nothing to act on.** No row below a root exists until the scan lands, so applying a saved set in the frame's constructor silently did nothing. Every `rebuild()` re-applies the remembered set, which is what makes the restore land whenever the rows appear.
- **`DeleteAllItems()` raises a collapse event per expanded row.** Read as the user closing folders, that emptied the remembered set one rebuild at a time and the tree came back fully collapsed after a filter was typed and cleared. The `applying_expansion_` guard is set *before* the delete, not just around the expanding that follows.

The set is the user's own expanding and collapsing, deliberately not what is on screen: a filter opens everything down to its matches, and saving that would reopen the whole tree next launch.

### A bound the walk can hit has to be reportable, and folders have to be skippable

Add a whole workspace folder as a root and the walk can meet far more than it expects: one measured well over a million entries against a bound of 100,000, with the overwhelming majority of them inside a single generated cache folder. The walk stopped partway through the alphabet, every folder after that point was simply absent from the tree, and nothing on screen said so — the count beside the root read like a total. Two things came out of that, and both are load-bearing:

- **`RootScan::truncated`.** The ceiling is now high enough that no real tree reaches it, and reaching it marks the root `(partial — scan limit reached)`. A silent cap presents a short list as the whole answer, which is worse than being slow.
- **`wx_excluded_folders`** — folders the walk does not descend into, toggled from a folder's context menu. A built-in list of names to skip was considered and rejected: the folder that actually cost the scan was an app's own output directory with an app-specific name, not `node_modules` or `.git`, so the choice has to be the user's. An excluded folder still gets a row, marked `(excluded)`, because a pruned folder that leaves no trace is indistinguishable from an empty one *and* its row is the only place the exclusion can be undone.

**Roots may nest, and the tree has to cope.** A workspace folder added as a root and one folder inside it added as a second root make the same absolute folder reachable from two roots. `folder_item()`'s node cache was keyed on the path alone, so the second root's documents were hung on the first root's node — the file appeared twice under one root, not at all under the other, and the folder count beside it still read 1. The keys carry the root index now. This only surfaced once the scan reached deep enough to produce the collision, which is a good reminder that a bug fixed upstream can uncover one downstream.

## A preview theme changes two stylesheets and a body class — nothing else

`mdrender::Theme` picks between `github-markdown-light.css` + `highlight/github.min.css` and `notes-light.css` + `highlight/xcode.min.css`, and stamps `theme-github` / `theme-notes` on `<body>`. **`render_body()` has no theme parameter at all**, which is the whole design: the generated body HTML is byte-identical either way, so the ~30 golden files cannot be disturbed by adding or changing a theme. A test asserts exactly that, and it is the one to check first if a theme ever seems to break the corpus.

Three things worth knowing before touching this:

- **The body class is load-bearing, not decoration.** The template's inline `<style>` is emitted *after* the linked stylesheets, so it wins every tie on `.markdown-body`. A theme sheet that is not scoped under `body.theme-x` will silently lose its layout rules — the colours will apply and the geometry will not, which looks like a half-finished stylesheet rather than a specificity bug.
- **`highlight/xcode.min.css` is hand-written, not vendored**, because ITAR rules out fetching it. Its palette was measured off a reference PDF and transcribed against highlight.js v11 scope names. A wrong scope name fails *silently* as plain text, so it was verified by rendering a tagged fence and reading the span colours back out of the exported PDF. Do the same if you extend it — `assets/VERSIONS.md` records the palette.
- **An unknown theme name falls back to GitHub** rather than failing. The name comes from `config.json`, which a later build may have written, and settings are never load-bearing.

Known gap: a fence with **no language tag** is not highlighted in either theme, whereas the note app the Notes theme is modelled on auto-detects. Changing that would affect the GitHub theme too, so it was left alone.

## PDF export is WebView2 printing the preview, not a second renderer

`PreviewPane::export_pdf()` calls `ICoreWebView2_7::PrintToPdf` on the page already on screen. That is the whole point: it is Chromium laying out the very page the user is looking at, so the PDF carries the GitHub stylesheet, highlight.js colours, mermaid and KaTeX without any of it being reimplemented. A second PDF library rendering the Markdown again would agree with the preview by coincidence, and not for long.

Three things that are deliberate:

- **Print settings are created, not left null.** The defaults print a header, a footer and no backgrounds, which would stamp a URL and date on every page and drop the shading behind code blocks. `ShouldPrintBackgrounds` on, `ShouldPrintHeaderAndFooter` off.
- **Hyperlinks are free, and verified.** Chromium's print pipeline emits real link annotations, so links stay clickable; the blue is just the stylesheet's `#0969da` surviving, because there is no `@media print` rule anywhere in the bundled CSS to override it. Both were checked by reading the exported PDF back with PyMuPDF — annotation URIs and the text span colour — not by looking at it.
- **An export while the preview is still starting is refused, not queued.** The user asked for a PDF now; producing one seconds later, of a page they may have navigated away from, is worse than saying no.

`ICoreWebView2_7` is queried rather than assumed. The runtime is evergreen and long past that version, but a failure there is an old runtime, not a bug, and the message says so.

## The Lists menu writes ordinary Markdown, deliberately

Three commands append to three files in `MD_Internal`, a folder beside `MD_Inbox`: `logins.md` (a table), `ToDoList.md` (a checklist), `GrailDiary.md` (dated sections). The ruling when they were added was that **no special handling is required** — `MD_Internal` is scanned, counted, filtered and searched like any other folder, and its files are meant to be hand-edited. So:

- **Append, never rewrite.** The seed (title, and the table header where there is one) is written only when the file does not exist. A file the user has since reordered or edited must survive the next entry untouched.
- **The formatting is pure and tested; only `append_to_internal()` touches a disk.** That split is why `InternalNotes.cpp` is wx-free and compiled straight into the test binary.
- **Anything going into a table cell is escaped.** A literal `|` ends the cell and silently shifts every column after it; a newline ends the row. Both render as *something*, which is what makes them easy to ship.
- **`todo_seed()` ends with a blank line.** Without it the first `- [ ]` sat directly under a paragraph. Found by looking at the app, not by a test — the file still rendered, just not as a list.

**`logins.md` holds plaintext passwords.** That was the explicit choice, with one mitigation: `MD_Internal` gets a deny-everything `.gitignore` the moment it is created, because a document root may sit inside a git repo and two of this author's are public. The guard is written *before* the first file, since the gap between creating the folder and guarding it is exactly when a commit would sweep it up. The other exposure — the Contents search indexes the file like any document — is inherent to the choice and is documented in HELP.md rather than defended against.

**Task lists needed a renderer change.** `MD_FLAG_TASKLISTS` was off and the `<li>` handler ignored `is_task`, so `- [ ] thing` rendered as a bullet with literal brackets. That was never specific to `ToDoList.md`; any document using GitHub's syntax rendered wrong. The boxes are emitted `disabled`, as GitHub does: the preview is a view of the file, and a box you could tick without the file changing would be lying. No golden corpus file uses the syntax, so enabling it changed no frozen expectation — check that again before touching the flag.

## One window per session, and the race that broke it

The single-instance guard exists so a double-clicked `.md` is handed to the running window instead of starting a rival — both instances write `config.json` when they close, and the last one silently discards the other's layout, expanded folders and recents.

It was implemented as a window property plus `WM_COPYDATA`, and a window property cannot be set until there *is* a window. Two launches in the same instant therefore both looked, both found nothing, and both opened. **The installer reproduces this every time**: its `[Run]` entry launches the app while Inno's `isreadme` flag opens `README.md`, which this app is the registered handler for. Two live instances were sitting there after a routine install, started in the same second.

The slot is now claimed with a **named mutex at process start, before any window exists**, and a later launch then *waits* for the first one's window before handing over. Three things about that are deliberate:

- **Claim first, look second.** The other order is the bug.
- **The wait is bounded** (100 × 100 ms). If the owner is stuck or died between claiming and showing, the caller opens its own window rather than exiting and taking the user's document with it.
- **`bInitialOwner` is FALSE** — only the existence of the name is being tested, and taking ownership would drag in abandoned-mutex handling for nothing. The name lives only while a handle is open, so a crashed instance frees the slot automatically.

Verified by launching two copies back-to-back with a document on the second: the released v1.5.0 gives two instances 3 times out of 3, the fix gives one 3 times out of 3 with the document forwarded. A test cannot cover this — it needs two real processes — so re-check it by hand if this code is touched.

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

Three traps, all learned the hard way:

- **Do not pipe it through `2>&1 |`.** PowerShell 5.1 wraps a native command's
  stderr in ErrorRecords and `$ErrorActionPreference = "Stop"` turns a
  build tool's first stderr line into a terminating error. Run it bare.
- **`Set-Location` is not enough, and the script sets `[Environment]::CurrentDirectory` too.** `Set-Location` moves PowerShell's own location and leaves .NET's working directory at whatever the shell *process* started in. The version bump pairs `Test-Path` (PowerShell — finds the file) with `[IO.File]::ReadAllText` (.NET — resolves the same relative path somewhere else), so the script died on its first bump with a `DirectoryNotFoundException` naming a path half from this repo and half from the shell's start directory. It had always been broken; it only ever passed because the shell happened to start in the repo root. Native tools invoked here (`ISCC`) inherit the process directory too, so the same line covers them.
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
- **`app/LogoAsset.h` is generated** — the tech-note banner logo as base64,
  produced from `background-logo.png` by `MDBossCpp/tools/embed_logo.py`. Never
  hand-edit it; re-run the script if the artwork changes. It is the single
  source of truth for that image: `Templates.cpp` decodes it both to build the
  `data:` URI the TechNote starter carries and to write the `.png` beside a
  saved note.
- **Starter templates are seeded per name, not per folder** (`Config`'s
  `wx_seeded_templates`). The old rule was "seed only if the templates folder
  does not exist", which meant a starter added in a later version could never
  reach an existing profile. A name is recorded as offered whether or not the
  file was written, so a deleted template still stays deleted, and a folder
  that predates the key has its original two starters adopted rather than
  rewritten. Adding a starter is therefore just a new entry in `starters()`.

## ITAR

Everything under `C:\source` is export-controlled technical data, and the rules
in the global `~/.claude/CLAUDE.md` apply — but note this repo is **public**, so
repo permissions are not a backstop. Decide what may be committed *before* the
commit, not before the push.

**The documentation is the leak path here, not the code.** Two near-misses,
both caught only by grepping the diff before staging:

- **Examples written from what you just tested.** Documenting the Contents
  search meant pasting a result — and the result was real filenames from the
  author's controlled work plus the matching lines from inside those documents.
  Screenshots, sample trees, example output and test fixtures all have this
  shape: the natural illustration is the live data. Invent neutral names
  (`install-guide.md`, a root called `Notes`) instead.
- **Starter content carrying organisational identity.** The TechNote template
  originally reproduced the author's real banner line, unit and programme
  identifiers included. What ships is the generic form; the author's own
  `%APPDATA%` copy keeps the real one, which per-name seeding never overwrites.

Before any commit, grep the staged files for project names, unit identifiers,
real root paths and document titles — not just for the obvious markers. And
`tn.md` at the repo root is git-ignored on purpose: it is a working note
holding a real tech-note title, and `release.ps1` refusing a dirty tree makes
`git add -A` the tempting way out.
