<#
Release MD Boss: bump the version in app.py + installer.iss, build the
one-dir app folder, the installer, and the portable zip, commit and push the
bump, publish a GitHub release with both assets, then reinstall locally.

Usage:
  .\release.ps1 0.1.0
  .\release.ps1 0.1.0 -NotesFile notes.md      # release notes from a file
  .\release.ps1 0.1.0 -Notes "- fixed X"       # inline release notes
  .\release.ps1 0.1.0 -SkipInstall             # don't reinstall/relaunch here

Refuses to build unless pytest, ruff and mypy --strict all pass (see the
quality gate below); that check runs before the version bump, so a failure
leaves the tree untouched.

Without -Notes/-NotesFile the GitHub notes are auto-generated from commits.
Requires: python (with PyInstaller, pytest, ruff, mypy), Inno Setup 6,
gh (authenticated), git. Windows PowerShell 5.1 compatible.
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
Write-Host "==> Bumping version to $Version" -ForegroundColor Cyan
$appPy = Join-Path $PSScriptRoot "app.py"
$iss   = Join-Path $PSScriptRoot "installer.iss"

$text = [IO.File]::ReadAllText($appPy)
if ($text -notmatch 'APP_VERSION = "[^"]+"') { Fail "APP_VERSION line not found in app.py" }
[IO.File]::WriteAllText($appPy, ($text -replace 'APP_VERSION = "[^"]+"', "APP_VERSION = `"$Version`""))

$text = [IO.File]::ReadAllText($iss)
if ($text -notmatch '#define AppVersion "[^"]+"') { Fail "AppVersion line not found in installer.iss" }
[IO.File]::WriteAllText($iss, ($text -replace '#define AppVersion "[^"]+"', "#define AppVersion `"$Version`""))

# --- Build -------------------------------------------------------------------
try { Stop-Process -Name MDBoss -Force -Confirm:$false -ErrorAction Stop
      Write-Host "==> Stopped running MD Boss" -ForegroundColor Cyan } catch {}

Write-Host "==> Building exe (PyInstaller)" -ForegroundColor Cyan
python -m PyInstaller MDBoss.spec --noconfirm
CheckExit "PyInstaller"

Write-Host "==> Building installer (ISCC)" -ForegroundColor Cyan
& $iscc installer.iss
CheckExit "ISCC"

# One-dir build: the zip holds the whole MDBoss folder, and its root entry is
# "MDBoss\" -- the updater looks for MDBoss.exe at either level.  The asset is
# deliberately NOT called MDBoss-Portable.zip any more; see the comment on
# UPDATE_PORTABLE_ASSET_NAME in app.py before renaming it back.
Write-Host "==> Building portable zip" -ForegroundColor Cyan
if (-not (Test-Path dist\MDBoss\MDBoss.exe)) { Fail "expected dist\MDBoss\MDBoss.exe (one-dir build)" }
Compress-Archive -Force -Path dist\MDBoss -DestinationPath installer\MDBoss-Portable-App.zip

# Both asset names are load-bearing: the in-app updater matches them exactly.
foreach ($asset in "installer\MDBoss-Setup.exe", "installer\MDBoss-Portable-App.zip") {
    if (-not (Test-Path $asset)) { Fail "expected artifact missing: $asset" }
}

# --- Commit + push -----------------------------------------------------------
git add app.py installer.iss
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
$ghArgs = @("release", "create", "v$Version",
            "installer\MDBoss-Setup.exe", "installer\MDBoss-Portable-App.zip",
            "--title", "v$Version")
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
