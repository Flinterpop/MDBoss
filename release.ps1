<#
Release MD Boss: bump the version everywhere it appears, build the C++ app's
installer and portable zip, commit and push the bump, publish a GitHub
release with both assets, then reinstall locally.

The release is the C++ port on Windows ONLY.  The Python app (app.py +
mdrender.py) is DEPRECATED as of v1.2.2: it is kept in-tree as a historical
reference and is no longer the parity oracle, is no longer built, gated, or
shipped, and Linux is no longer a supported target -- the AppImage is gone.
Anyone still running the old Python build (Windows or the last AppImage) gets
no further updates; their updater finds no matching asset and falls back to
opening the releases page.  MDBoss-Setup.exe, MDBoss-Portable-App.zip and
the AppImage must NEVER reappear as asset names: an old install seeing its
asset name would silently "update" itself with whatever the asset holds.

The version still lives in seven places and is bumped in lockstep:
app.py, installer.iss, installer-cpp.iss, MDBossCpp/app/Version.h,
MDBossCpp/CMakeLists.txt, and BOTH the string and numeric forms in
MDBossCpp/app/MDBoss.rc.  app.py and installer.iss are deprecated and no
longer built here, but they stay in the lockstep -- a file that drifts is a
file that ships wrong the day it is resurrected.  This script owns all seven;
bumping by hand is how they drift.

Usage:
  .\release.ps1 0.1.0
  .\release.ps1 0.1.0 -NotesFile notes.md      # release notes from a file
  .\release.ps1 0.1.0 -Notes "- fixed X"       # inline release notes
  .\release.ps1 0.1.0 -SkipInstall             # don't reinstall/relaunch here

ctest runs AFTER the version bump, because test_version.cpp is what proves
the bump reached every file -- before the bump it would only prove they
agreed at the old number.  The Python suite (pytest/ruff/mypy) no longer
gates the release: the C++ ctest suite is the gate now.

Without -Notes/-NotesFile the GitHub notes are auto-generated from commits.
Requires: CMake with a Visual Studio toolchain and vcpkg for the C++ port,
Inno Setup 6, gh (authenticated), git. Windows PowerShell 5.1 compatible.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [string]$Notes = "",
    [string]$NotesFile = "",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
# BOTH lines, and the second is not redundant.  Set-Location moves PowerShell's
# own location; it does NOT touch .NET's working directory, which is whatever
# the shell PROCESS was started in.  The version bump below pairs Test-Path
# (PowerShell, so it finds the file) with [IO.File]::ReadAllText (.NET, so it
# resolves the same relative path somewhere else entirely) -- the failure is a
# DirectoryNotFoundException naming a path that is half this repo and half the
# directory the shell happened to start in.  Running this script from a shell
# opened anywhere but the repo root is enough to hit it; the sibling app's
# release script had the identical bug and it was found there first.
[Environment]::CurrentDirectory = $PSScriptRoot

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
foreach ($tool in "git", "gh") {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool not on PATH" }
}
if ($NotesFile -and -not (Test-Path $NotesFile)) { Fail "notes file not found: $NotesFile" }

$dirty = git status --porcelain
if ($dirty) { Fail "working tree not clean -- commit or stash first:`n$dirty" }

# The Python quality gate (pytest/ruff/mypy) used to run here.  It was removed
# when the Python app was deprecated (v1.2.2): it is no longer the reference
# implementation, no longer built, and no longer shipped, so its suite no
# longer gates the C++ release.  ctest, run after the bump below, is the gate.
# The Python code stays in-tree and can still be run and tested by hand.

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

# The portable zip mirrors installer-cpp.iss's [Files] exactly: the exe, the
# render assets beside it, HELP.md and README.md, all under a "MDBoss\" root
# folder -- portable_batch in Updater.cpp accepts the exe at the root or one
# folder down, same as app.py did.  Staged fresh every time so a stale file
# from an earlier layout cannot ride along.
Write-Host "==> Building C++ portable zip" -ForegroundColor Cyan
$stage = "installer\portable-stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$null = New-Item -ItemType Directory -Force "$stage\MDBoss"
Copy-Item MDBossCpp\build\app\Release\MDBoss.exe "$stage\MDBoss\"
Copy-Item -Recurse assets "$stage\MDBoss\assets"
Copy-Item HELP.md, README.md "$stage\MDBoss\"
Compress-Archive -Force -Path "$stage\MDBoss" `
    -DestinationPath installer\MDBoss-Cpp-Portable.zip
Remove-Item -Recurse -Force $stage

# Every asset name is load-bearing: the in-app updater matches its own assets
# exactly (kSetupAssetName / kPortableAssetName in Updater.cpp), and the old
# Python / AppImage asset names must NOT reappear -- an old install seeing its
# asset name here would silently "update" itself with whatever it downloads.
# There is no Linux AppImage any more: Linux was dropped when the Python app
# was deprecated (v1.2.2).  The last AppImage users get no further updates.
$assets = @("installer\MDBoss-Cpp-Setup.exe",
            "installer\MDBoss-Cpp-Portable.zip")
foreach ($asset in $assets) {
    if (-not (Test-Path $asset)) { Fail "expected artifact missing: $asset" }
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
# The C++ app now.  A silent run keeps the scope of an existing install
# (Inno's UsePreviousPrivileges); a first-ever install takes the per-machine
# default, so expect one UAC prompt in that case.  The exe is looked up in
# both scopes' folders rather than assumed.
if (-not $SkipInstall) {
    Write-Host "==> Reinstalling locally and relaunching" -ForegroundColor Cyan
    $setup = Start-Process (Join-Path $PSScriptRoot "installer\MDBoss-Cpp-Setup.exe") `
        -ArgumentList "/VERYSILENT", "/NORESTART", "/SUPPRESSMSGBOXES" -Wait -PassThru
    if ($setup.ExitCode -ne 0) {
        # 2 = cancelled, which includes a declined or unseen UAC prompt: a
        # fresh install defaults to per-machine and needs elevation.
        Write-Host ("    installer exited with code $($setup.ExitCode) -- " +
                    "not installed (declined UAC?). The release itself is " +
                    "already published.") -ForegroundColor Yellow
    } else {
        $exe = @("$env:ProgramFiles\MD Boss Cpp\MDBoss.exe",
                 "$env:LOCALAPPDATA\Programs\MD Boss Cpp\MDBoss.exe") |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($exe) { Start-Process $exe }
        else { Write-Host "    installed, but MDBoss.exe was not found to relaunch" -ForegroundColor Yellow }
    }
}

Write-Host "==> Done: https://github.com/Flinterpop/MDBoss/releases/tag/v$Version" -ForegroundColor Green
