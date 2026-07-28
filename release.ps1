<#
Release MD Boss: bump the version everywhere it appears, build both Windows
apps and the Linux AppImage, commit and push the bump, publish a GitHub
release with every asset, then reinstall locally.

The AppImage is built through WSL from this same working tree.  It is part of
the release rather than a manual step afterwards because the manual step got
missed once, and a release without it silently breaks self-update for anyone
already running one.  -SkipAppImage opts out; there is no way to omit it by
accident.

The repo holds two apps at one version -- the shipping Python app and the C++
port -- so the version lives in seven places: app.py, installer.iss,
installer-cpp.iss, MDBossCpp/app/Version.h, MDBossCpp/CMakeLists.txt, and BOTH
the string and numeric forms in MDBossCpp/app/MDBoss.rc.  This script owns all
seven; bumping by hand is how they drift.

Usage:
  .\release.ps1 0.1.0
  .\release.ps1 0.1.0 -NotesFile notes.md      # release notes from a file
  .\release.ps1 0.1.0 -Notes "- fixed X"       # inline release notes
  .\release.ps1 0.1.0 -SkipInstall             # don't reinstall/relaunch here
  .\release.ps1 0.1.0 -SkipAppImage            # Windows assets only (see below)

Refuses to build unless pytest, ruff and mypy --strict all pass (see the
quality gate below); that check runs before the version bump, so a failure
leaves the tree untouched.  ctest runs AFTER the bump instead, because
test_version.cpp is what proves the bump reached every file -- before the
bump it would only prove they agreed at the old number.

Without -Notes/-NotesFile the GitHub notes are auto-generated from commits.
Requires: python (with PyInstaller, pytest, ruff, mypy), CMake with a Visual
Studio toolchain and vcpkg for the C++ port, Inno Setup 6, gh (authenticated),
git. Windows PowerShell 5.1 compatible.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [string]$Notes = "",
    [string]$NotesFile = "",
    [switch]$SkipInstall,
    # Publish Windows assets only.  Deliberately opt-OUT: forgetting the
    # AppImage is silent, and refusing it has to be a decision someone made.
    [switch]$SkipAppImage
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function CheckExit($what) {
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit $LASTEXITCODE)" }
}

# --- Preflight ---------------------------------------------------------------
$iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) {
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source } else { Fail "ISCC.exe not found (Inno Setup 6)" }
}
foreach ($tool in "python", "git", "gh") {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool not on PATH" }
}
if ($NotesFile -and -not (Test-Path $NotesFile)) { Fail "notes file not found: $NotesFile" }

$dirty = git status --porcelain
if ($dirty) { Fail "working tree not clean -- commit or stash first:`n$dirty" }

# --- Quality gate ------------------------------------------------------------
# Runs before the version bump, so a failure leaves the tree exactly as it was.
# Configured in pyproject.toml; both tools are expected to pass clean, so any
# output here is a real regression rather than pre-existing noise.
# Do not pipe these through 2>&1: PowerShell 5.1 wraps a native command's
# stderr in ErrorRecords, and $ErrorActionPreference = "Stop" then turns an
# ordinary progress line into a terminating error.
Write-Host "==> Tests" -ForegroundColor Cyan
python -m pytest -q
CheckExit "pytest"

Write-Host "==> Lint (ruff)" -ForegroundColor Cyan
python -m ruff check .
CheckExit "ruff"

Write-Host "==> Types (mypy --strict)" -ForegroundColor Cyan
python -m mypy
CheckExit "mypy"

# --- Bump versions -----------------------------------------------------------
# Seven places, not two.  The C++ port carries the same version number, and
# MDBossCpp/tests/test_version.cpp fails the build when they disagree -- so
# bumping only the Python pair leaves the other tree broken, discovered at the
# next C++ build rather than here.  The .rc needs BOTH forms: Explorer reads
# the string, installers and update checks compare the numeric tuple.
Write-Host "==> Bumping version to $Version" -ForegroundColor Cyan

$parts = $Version.Split(".")
if ($parts.Count -ne 3) { Fail "version must be major.minor.patch, got '$Version'" }
$tuple = "$($parts[0]),$($parts[1]),$($parts[2]),0"

# Each entry is the file, a regex that must already match, and its replacement.
# Requiring a match first means a renamed constant fails loudly here instead of
# silently leaving one file behind.
$bumps = @(
    @{ File = "app.py";
       Match = 'APP_VERSION = "[^"]+"';
       New   = "APP_VERSION = `"$Version`"" },
    @{ File = "installer.iss";
       Match = '#define AppVersion "[^"]+"';
       New   = "#define AppVersion `"$Version`"" },
    @{ File = "installer-cpp.iss";
       Match = '#define AppVersion "[^"]+"';
       New   = "#define AppVersion `"$Version`"" },
    @{ File = "MDBossCpp\app\Version.h";
       Match = 'kAppVersion = "[^"]+"';
       New   = "kAppVersion = `"$Version`"" },
    @{ File = "MDBossCpp\CMakeLists.txt";
       Match = 'project\(MDBossCpp VERSION [0-9.]+';
       New   = "project(MDBossCpp VERSION $Version" },
    @{ File = "MDBossCpp\app\MDBoss.rc";
       Match = 'FILEVERSION\s+\d+,\d+,\d+,\d+';
       New   = "FILEVERSION    $tuple" },
    @{ File = "MDBossCpp\app\MDBoss.rc";
       Match = 'PRODUCTVERSION\s+\d+,\d+,\d+,\d+';
       New   = "PRODUCTVERSION $tuple" },
    @{ File = "MDBossCpp\app\MDBoss.rc";
       Match = 'VALUE "FileVersion",\s+"[^"]+"';
       New   = "VALUE `"FileVersion`",      `"$Version`"" },
    @{ File = "MDBossCpp\app\MDBoss.rc";
       Match = 'VALUE "ProductVersion",\s+"[^"]+"';
       New   = "VALUE `"ProductVersion`",   `"$Version`"" }
)

foreach ($bump in $bumps) {
    $path = Join-Path $PSScriptRoot $bump.File
    if (-not (Test-Path $path)) { Fail "version file not found: $($bump.File)" }
    # ReadAllText/WriteAllText, never Get-Content|Set-Content: the latter round
    # trips UTF-8 through the ANSI code page and mangles every non-ASCII
    # character in the file.  MDBossCpp/tests/test_sources.cpp guards it.
    $text = [IO.File]::ReadAllText($path)
    if ($text -notmatch $bump.Match) {
        Fail "pattern not found in $($bump.File): $($bump.Match)"
    }
    [IO.File]::WriteAllText($path, ($text -replace $bump.Match, $bump.New))
}

# --- Build -------------------------------------------------------------------
try { Stop-Process -Name MDBoss -Force -Confirm:$false -ErrorAction Stop
      Write-Host "==> Stopped running MD Boss" -ForegroundColor Cyan } catch {}

Write-Host "==> Building exe (PyInstaller)" -ForegroundColor Cyan
python -m PyInstaller MDBoss.spec --noconfirm
CheckExit "PyInstaller"

Write-Host "==> Building installer (ISCC)" -ForegroundColor Cyan
& $iscc installer.iss
CheckExit "ISCC"

# --- C++ port ----------------------------------------------------------------
# Built and tested AFTER the bump, deliberately: test_version.cpp compares the
# version across every file that carries one, so running it here is what
# proves the bump actually reached all seven.  Running it before the bump
# would only prove they agreed at the old number.
Write-Host "==> Configuring C++ port (CMake)" -ForegroundColor Cyan
cmake -S MDBossCpp -B MDBossCpp\build
CheckExit "cmake configure"

Write-Host "==> Building C++ port" -ForegroundColor Cyan
cmake --build MDBossCpp\build --config Release
CheckExit "cmake build"

Write-Host "==> Tests (ctest)" -ForegroundColor Cyan
ctest --test-dir MDBossCpp\build -C Release --output-on-failure
CheckExit "ctest"

Write-Host "==> Building C++ installer (ISCC)" -ForegroundColor Cyan
& $iscc installer-cpp.iss
CheckExit "ISCC (C++)"

# One-dir build: the zip holds the whole MDBoss folder, and its root entry is
# "MDBoss\" -- the updater looks for MDBoss.exe at either level.  The asset is
# deliberately NOT called MDBoss-Portable.zip any more; see the comment on
# UPDATE_PORTABLE_ASSET_NAME in app.py before renaming it back.
Write-Host "==> Building portable zip" -ForegroundColor Cyan
if (-not (Test-Path dist\MDBoss\MDBoss.exe)) { Fail "expected dist\MDBoss\MDBoss.exe (one-dir build)" }
Compress-Archive -Force -Path dist\MDBoss -DestinationPath installer\MDBoss-Portable-App.zip

# Every asset name is load-bearing: each app's in-app updater matches its own
# exactly.  MDBoss-Cpp-Setup.exe is what Updater.h's kSetupAssetName looks for.
$assets = @("installer\MDBoss-Setup.exe",
            "installer\MDBoss-Portable-App.zip",
            "installer\MDBoss-Cpp-Setup.exe")
foreach ($asset in $assets) {
    if (-not (Test-Path $asset)) { Fail "expected artifact missing: $asset" }
}

# --- Linux AppImage ----------------------------------------------------------
# Built here rather than by hand afterwards, because by hand is how v1.1.0
# shipped without it: an existing AppImage then saw a newer version, accepted
# the update, and was sent to a releases page carrying nothing it could use.
# Nothing about that failure was loud.
#
# It is a Linux build driven from Windows through WSL, from this same working
# tree, so it picks up the version bump above without a second checkout.
$appImage = "dist\MDBoss-x86_64.AppImage"
$appImageZsync = "$appImage.zsync"
$wslOk = $false
if (-not $SkipAppImage) {
    $null = wsl.exe --list --quiet 2>$null
    $wslOk = ($LASTEXITCODE -eq 0)
    if (-not $wslOk) {
        Fail ("WSL is not available, so the Linux AppImage cannot be built. " +
              "Re-run with -SkipAppImage to publish Windows assets only -- " +
              "but a release without the AppImage breaks self-update for " +
              "everyone already running one.")
    }
}
if ($wslOk) {
    # build-appimage.sh resolves this with gh, which is not installed inside
    # WSL; the Windows gh is, so it is resolved here and passed in.
    Write-Host "==> Resolving python-build-standalone" -ForegroundColor Cyan
    $pbsRelease = gh api repos/astral-sh/python-build-standalone/releases/latest | ConvertFrom-Json
    $pbsUrl = $pbsRelease.assets |
        Where-Object { $_.name -match 'cpython-3\.12\.\d+.*x86_64-unknown-linux-gnu-install_only\.tar\.gz$' -and
                       $_.name -notmatch 'debug' } |
        Select-Object -First 1 -ExpandProperty browser_download_url
    if (-not $pbsUrl) { Fail "could not resolve a python-build-standalone build" }

    # The DEFAULT distro, whatever it is called -- verified against Ubuntu.
    # Not pinned by name because the name differs between machines, and a
    # wrong pin fails the release outright rather than degrading.
    Write-Host "==> Building Linux AppImage (WSL)" -ForegroundColor Cyan
    wsl.exe -- bash -lc "cd /mnt/c/source/MDBoss && PBS_URL='$pbsUrl' ./build-appimage.sh"
    CheckExit "build-appimage.sh"

    foreach ($a in @($appImage, $appImageZsync)) {
        if (-not (Test-Path $a)) { Fail "AppImage build produced no $a" }
    }
    # Both, always: the .zsync is what an installed AppImage reads to find the
    # update.  Shipping the AppImage without it leaves self-update broken just
    # as thoroughly as shipping neither.
    $assets += $appImage
    $assets += $appImageZsync
} else {
    Write-Host "==> SKIPPING the Linux AppImage (-SkipAppImage)" -ForegroundColor Yellow
    Write-Host "    Anyone running an AppImage will be offered this release" `
               -ForegroundColor Yellow
    Write-Host "    and find nothing in it they can install." -ForegroundColor Yellow
}

# --- Commit + push -----------------------------------------------------------
# All seven version-bearing files, or the commit records a half-done bump.
git add app.py installer.iss installer-cpp.iss `
        MDBossCpp\app\Version.h MDBossCpp\app\MDBoss.rc MDBossCpp\CMakeLists.txt
$staged = git diff --cached --name-only
if ($staged) {
    git commit -m "Bump version to $Version"
    CheckExit "git commit"
} else {
    Write-Host "==> Versions already at $Version, nothing to commit" -ForegroundColor Yellow
}

Write-Host "==> Syncing with origin" -ForegroundColor Cyan
git pull --rebase origin main
CheckExit "git pull --rebase"
git push origin main
CheckExit "git push"

# --- Publish release ---------------------------------------------------------
Write-Host "==> Publishing GitHub release v$Version" -ForegroundColor Cyan
$ghArgs = @("release", "create", "v$Version") + $assets + @("--title", "v$Version")
if ($NotesFile)  { $ghArgs += @("--notes-file", $NotesFile) }
elseif ($Notes)  { $ghArgs += @("--notes", $Notes) }
else             { $ghArgs += "--generate-notes" }
& gh @ghArgs
CheckExit "gh release create"

# --- Local reinstall ---------------------------------------------------------
if (-not $SkipInstall) {
    Write-Host "==> Reinstalling locally and relaunching" -ForegroundColor Cyan
    Start-Process (Join-Path $PSScriptRoot "installer\MDBoss-Setup.exe") `
        -ArgumentList "/VERYSILENT", "/NORESTART", "/SUPPRESSMSGBOXES" -Wait
    Start-Process "$env:LOCALAPPDATA\Programs\MD Boss\MDBoss.exe"
}

Write-Host "==> Done: https://github.com/Flinterpop/MDBoss/releases/tag/v$Version" -ForegroundColor Green
