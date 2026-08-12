"""MDBoss -- a local Markdown manager, editor, and GitHub-style viewer.

MDBoss browses Markdown files across up to five root folders, edits them in a
source pane, and renders a live preview using the GitHub-light theme with
mermaid diagrams and embedded images.  It also opens any file on disk -- via
Ctrl+O, a drop, or a path on the command line -- so it works as a plain viewer
and as the system's Markdown handler, one window per session.

It is a PySide6 sibling of PDF Sherpa and reuses that app's conventions: a
single-file GUI monolith over a small pure helper module (``mdrender``) and a
read-merge-write JSON config (``%APPDATA%`` on Windows, XDG on Linux -- see
``_user_data_base``).  Windows ships as PyInstaller -> Inno installer plus a
portable zip; Linux ships as an AppImage built by ``build-appimage.sh``.  Both
self-update from the GitHub releases.  Platform-specific code is guarded with
``sys.platform.startswith("win")``.

Rendering is 100% offline (bundled mermaid + CSS + Pygments) and the preview's
web view is network-locked: every request whose scheme is not local is blocked,
so a stray remote image or link in a document can never reach the network.
"""

from __future__ import annotations

import datetime
import json
import ntpath
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.request
import webbrowser
import zipfile
from collections.abc import Callable
from typing import TypedDict

# Windows-only standard modules.  Importing these unconditionally would make
# the app fail to start on Linux, where the file-association code they serve
# has no counterpart -- install-linux.sh registers text/markdown in the
# .desktop entry instead.
if sys.platform.startswith("win"):
    import ctypes
    import winreg

from PySide6.QtCore import (
    QByteArray, QEvent, QObject, QPoint, QRect, QSize, Qt, QTimer, QUrl, Signal,
    Slot,
)
from PySide6.QtGui import (
    QAction, QCloseEvent, QColor, QDragEnterEvent, QDragMoveEvent, QDropEvent,
    QFont, QIcon, QKeySequence, QPaintEvent, QPainter, QResizeEvent,
    QTextCharFormat, QTextFormat,
)
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWebChannel import QWebChannel
from PySide6.QtWebEngineCore import (
    QWebEnginePage, QWebEngineProfile, QWebEngineSettings,
    QWebEngineUrlRequestInfo, QWebEngineUrlRequestInterceptor,
)
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import (
    QApplication, QDialog, QDialogButtonBox, QFileDialog,
    QHBoxLayout, QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem,
    QMainWindow, QMenu, QMessageBox, QPlainTextEdit, QProgressDialog,
    QPushButton, QSplitter, QTextEdit, QToolBar, QToolButton, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
)

import mdrender

APP_VERSION = "1.2.3"
APP_NAME = "MDBoss"            # config folder, exe name, process name
DISPLAY_NAME = "MD Boss"       # human-facing name (installer, window title)

# In-app updater: asset names are load-bearing -- release.ps1 publishes exactly
# these and the updater matches them by name.
UPDATE_API_URL = (
    "https://api.github.com/repos/Flinterpop/MDBoss/releases/latest"
)
UPDATE_ASSET_NAME = "MDBoss-Setup.exe"
# Renamed from MDBoss-Portable.zip when the Windows build went one-dir.  Do not
# change it back: v0.1.11 and earlier match that old name, then move the first
# .exe they find in the zip over their own -- which for a one-dir zip means the
# 7 MB stub without its _internal folder, leaving an app that cannot start.
# Finding no asset by that name, those versions fall back to opening the
# releases page, which is the right answer for a layout change.
UPDATE_PORTABLE_ASSET_NAME = "MDBoss-Portable-App.zip"
UPDATE_APPIMAGE_ASSET_NAME = "MDBoss-x86_64.AppImage"
RELEASES_URL = "https://github.com/Flinterpop/MDBoss/releases/latest"

MAX_ROOTS = 5
MAX_FAVORITES = 10
MAX_RECENTS = 6                # documents listed in the Recent panel
MARKDOWN_EXTS = (".md", ".markdown", ".mdown", ".mkd", ".mdwn")
# File-dialog filter, kept in step with MARKDOWN_EXTS.
MARKDOWN_FILTER = (
    "Markdown (" + " ".join("*" + ext for ext in MARKDOWN_EXTS) + ")"
)
# Optional landing folder for documents copied in via "Import files into
# MD_Inbox…".  Recognised when a root is named this, or holds a top-level
# subfolder of this name.  Dropping a file does NOT copy here -- drops open the
# file where it lies (see _perform_drop).
INBOX_NAME = "MD_Inbox"
# Drag/drop event types routed through the app-level filter so a drop is seen
# whatever widget is under the cursor (the preview's native web widget would
# otherwise swallow it).
_DND_EVENT_TYPES = frozenset({
    QEvent.Type.DragEnter, QEvent.Type.DragMove, QEvent.Type.Drop,
})
PREVIEW_DEBOUNCE_MS = 300
# Named pipe used to keep one window per user session.  As the default handler
# for .md files, MD Boss is launched once per double-clicked document; without
# this each launch would be a separate process, and the last one to close would
# overwrite the others' recents, favorites and window layout.
IPC_SERVER_NAME = "MDBoss.instance"
IPC_TIMEOUT_MS = 1000

# Item-data roles on tree items.
ROLE_PATH = int(Qt.ItemDataRole.UserRole)
ROLE_KIND = int(Qt.ItemDataRole.UserRole) + 1   # "root" | "dir" | "file"

# A saved settings blob and a single root-folder entry.
ConfigDict = dict[str, object]
Root = dict[str, str]


class ReleaseInfo(TypedDict):
    """The subset of a GitHub release the updater cares about."""

    version: tuple[int, ...]
    version_str: str
    asset_url: str | None
    portable_url: str | None
    appimage_url: str | None
    html_url: str


# --------------------------------------------------------------------------- #
# Pure helpers (config, resources, updater) -- mirror PDF Sherpa's patterns.
# --------------------------------------------------------------------------- #
def _user_data_base() -> str:
    """Root under which per-user data (config, templates) lives.

    Windows keeps everything under ``%APPDATA%``.  On Linux/macOS we follow
    the XDG base-dir spec (``$XDG_CONFIG_HOME`` or ``~/.config``) so MDBoss's
    settings sit beside every other app's rather than dumped in the home
    directory.  ``APPDATA`` is still honoured first if it happens to be set
    (e.g. under Wine) to keep behaviour identical to the Windows build."""
    appdata = os.environ.get("APPDATA")
    if appdata:
        return appdata
    if sys.platform.startswith("win"):
        return os.path.expanduser("~")
    return (os.environ.get("XDG_CONFIG_HOME")
            or os.path.join(os.path.expanduser("~"), ".config"))


def config_path() -> str:
    """Per-user settings file, stable across source and frozen runs."""
    base = _user_data_base()
    return os.path.join(base, APP_NAME, "config.json")


def load_config() -> ConfigDict:
    """Return saved settings, or an empty dict on any read/parse failure."""
    try:
        with open(config_path(), encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_config(settings: ConfigDict) -> None:
    """Persist ``settings``; never raise -- settings are a convenience."""
    assert isinstance(settings, dict), "settings must be a dict"
    try:
        path = config_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(settings, fh, indent=2)
    except OSError:
        pass


def update_config(updates: ConfigDict) -> None:
    """Merge ``updates`` into saved settings (preserves other keys)."""
    assert isinstance(updates, dict), "updates must be a dict"
    cfg = load_config()
    cfg.update(updates)
    save_config(cfg)


def resource_path(name: str) -> str:
    """Locate a bundled resource, whether running from source or frozen."""
    assert name, "resource name must be non-empty"
    base = getattr(
        sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__))
    )
    return os.path.join(base, name)


# Well-known Windows user folders whose Linux counterparts sit under $HOME.
_WIN_HOME_ANCHORS = {
    "dropbox": "Dropbox", "onedrive": "OneDrive", "documents": "Documents",
    "desktop": "Desktop", "downloads": "Downloads",
}


def translate_windows_path(path: str) -> str:
    """Best-effort remap of a Windows favorite path onto this machine.

    The Windows build stores favorites like ``J:\\Dropbox\\03_Work\\note.md``.
    On Linux/macOS the same tree usually lives under ``$HOME``, so convert the
    separators and, when the path passes through a well-known user folder
    (Dropbox, OneDrive, Documents, ...), re-anchor it there.  Paths that
    already look POSIX -- or that we cannot place -- are returned unchanged, so
    calling this on native favorites is a no-op."""
    looks_windows = "\\" in path or (
        len(path) >= 2 and path[1] == ":" and path[0].isalpha()
    )
    if not looks_windows:
        return path
    posix = path.replace("\\", "/")
    if len(posix) >= 2 and posix[1] == ":" and posix[0].isalpha():
        posix = posix[2:]                        # drop the drive letter
    parts = [p for p in posix.split("/") if p]
    home = os.path.expanduser("~")
    for i, part in enumerate(parts):
        anchor = _WIN_HOME_ANCHORS.get(part.lower())
        if anchor:
            return os.path.join(home, anchor, *parts[i + 1:])
    return "/" + "/".join(parts)                 # last resort: make absolute


def running_appimage() -> str | None:
    """Path to the running AppImage, or None when not launched from one.

    The AppImage runtime exports ``APPIMAGE`` (the outer .AppImage path) to the
    program it launches; its presence is how we know an in-place self-update is
    possible on Linux."""
    path = os.environ.get("APPIMAGE")
    return path if path and os.path.isfile(path) else None


def running_portable() -> bool:
    """True when this frozen exe is a loose portable copy (no uninstaller)."""
    if not getattr(sys, "frozen", False):
        return False
    try:
        names = os.listdir(os.path.dirname(sys.executable))
    except OSError:
        return True
    return not any(
        n.lower().startswith("unins") and n.lower().endswith(".exe")
        for n in names
    )


def parse_version(text: str) -> tuple[int, ...] | None:
    """'v1.2.3' / '1.2.3' -> (1, 2, 3); None if malformed."""
    try:
        return tuple(int(p) for p in text.lstrip("vV").split("."))
    except ValueError:
        return None


def fetch_latest_release() -> ReleaseInfo:
    """Query GitHub for the newest release.  Blocking -- worker thread only.

    Returns a dict with ``version`` (tuple), ``version_str``, ``asset_url``,
    ``portable_url`` and ``html_url``.  Raises on any failure.  This is the
    only outbound request MDBoss makes and it carries no document data.
    """
    req = urllib.request.Request(
        UPDATE_API_URL,
        headers={
            "User-Agent": APP_NAME,
            "Accept": "application/vnd.github+json",
        },
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = json.load(resp)
    version = parse_version(str(data.get("tag_name", "")))
    if version is None:
        raise ValueError("unrecognized release tag")
    asset = None
    portable = None
    appimage = None
    for entry in data.get("assets", []):
        name = str(entry.get("name", ""))
        url = entry.get("browser_download_url")
        if name == UPDATE_ASSET_NAME:
            asset = url
        elif name == UPDATE_PORTABLE_ASSET_NAME:
            portable = url
        elif name == UPDATE_APPIMAGE_ASSET_NAME:
            appimage = url
    return {
        "version": version,
        "version_str": str(data.get("tag_name", "")).lstrip("vV"),
        "asset_url": asset,
        "portable_url": portable,
        "appimage_url": appimage,
        "html_url": data.get("html_url", RELEASES_URL),
    }


def is_markdown(path: str) -> bool:
    """True when ``path`` has a recognised Markdown extension."""
    return os.path.splitext(path)[1].lower() in MARKDOWN_EXTS


def find_inbox(roots: list[Root]) -> str | None:
    """Return the path of an ``MD_Inbox`` drop folder among ``roots``.

    A root may itself be named ``MD_Inbox``, or hold one as a top-level
    subfolder.  Matching is case-insensitive; the first match wins.  Returns
    ``None`` when no such folder exists, which disables drag-and-drop ingest.
    """
    assert isinstance(roots, list), "roots must be a list"
    target = INBOX_NAME.lower()
    for root in roots:
        path = root.get("path", "")
        if not path or not os.path.isdir(path):
            continue
        if os.path.basename(os.path.normpath(path)).lower() == target:
            return path
        try:
            entries = list(os.scandir(path))
        except OSError:
            continue
        count = 0
        for entry in entries:
            count += 1
            if count > 20000:                      # bounded (Rule of 10)
                break
            if entry.is_dir() and entry.name.lower() == target:
                return entry.path
    return None


def unique_dest(dest_dir: str, filename: str) -> str:
    """A path in ``dest_dir`` for ``filename`` that will not overwrite.

    On collision, inserts ``" (2)"``, ``" (3)"`` … before the extension so an
    ingested file never clobbers one already in the inbox.
    """
    assert dest_dir, "dest_dir must be non-empty"
    assert filename, "filename must be non-empty"
    stem, ext = os.path.splitext(filename)
    candidate = os.path.join(dest_dir, filename)
    counter = 2
    while os.path.exists(candidate) and counter < 10000:   # bounded (Rule 10)
        candidate = os.path.join(dest_dir, f"{stem} ({counter}){ext}")
        counter += 1
    return candidate


# Header for update-handoff batches: wait until every MDBoss.exe process has
# exited (up to ~60s) before touching the on-disk exe.  A fixed delay is not
# enough -- shutdown can outlast it, and writing over a still-locked exe makes
# the installer fail, whereupon the old version relaunches and offers the same
# update again (an update loop).  This mattered more under the old one-file
# build, whose %TEMP% unpack took seconds to clean up on exit, but the exe lock
# outlives a fixed sleep either way, so the wait stays.
#
# The delay is `ping`, not `timeout`: _spawn_handoff_batch runs this with
# CREATE_NO_WINDOW, and with no console `timeout` exits at once with "Input
# redirection is not supported".  That silently collapsed all 60 iterations
# into ~4 seconds and let the copy start while the app was still running --
# exactly the failure this loop exists to prevent.  `ping -n N` waits N-1
# seconds and needs no console.
#
# Every tool is called by absolute path.  A PATH carrying GNU coreutils (Git
# for Windows ships one in usr/bin) shadows find.exe, and GNU find reads /I as
# a path and fails, which reports "no MDBoss.exe running" and skips the wait.
_SYS32 = r"%SystemRoot%\System32"
_WAIT_FOR_EXIT = (
    "@echo off\r\n"
    f'"{_SYS32}\\PING.EXE" -n 3 127.0.0.1 >nul\r\n'
    "set /a _n=0\r\n"
    ":mdwait\r\n"
    f'"{_SYS32}\\tasklist.exe" /FI "IMAGENAME eq MDBoss.exe" 2>nul | '
    f'"{_SYS32}\\find.exe" /I "MDBoss.exe" >nul\r\n'
    "if errorlevel 1 goto mdgo\r\n"
    "set /a _n+=1\r\n"
    "if %_n% GEQ 60 goto mdgo\r\n"
    f'"{_SYS32}\\PING.EXE" -n 2 127.0.0.1 >nul\r\n'
    "goto mdwait\r\n"
    ":mdgo\r\n"
)


def _install_scope_flag(app_exe: str) -> str:
    """Inno scope switch matching where this copy lives: /ALLUSERS under
    Program Files, /CURRENTUSER anywhere else.

    The installer asks per-user or per-machine and defaults to per-machine
    (Program Files), and a /VERYSILENT run takes that default -- so without an
    explicit switch a silent self-update of a per-user install would elevate
    and plant a second copy in Program Files instead of updating this one.
    Windows path semantics (ntpath) on purpose: the paths are Windows paths
    even when the test suite runs elsewhere.
    """
    assert app_exe, "no exe path to classify"
    exe = ntpath.normcase(app_exe)
    for name in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"):
        root = os.environ.get(name)
        if root and exe.startswith(ntpath.normcase(root) + "\\"):
            return "/ALLUSERS"
    return "/CURRENTUSER"


def _installer_batch(setup_path: str, app_exe: str) -> str:
    """Batch that installs an update and relaunches, run after we exit.

    Waits for every MDBoss.exe to exit (so the exe lock is released), installs
    silently -- pinned to the scope this copy is installed in -- relaunches,
    then cleans up.  Each line runs even if an earlier one failed, so a failed
    install still relaunches the intact old exe.
    """
    scope = _install_scope_flag(app_exe)
    return (
        _WAIT_FOR_EXIT
        + f'"{setup_path}" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES '
        + scope + "\r\n"
        + f'start "" "{app_exe}"\r\n'
        + f'del /q "{setup_path}"\r\n'
        + 'del /q "%~f0"\r\n'
    )


def _portable_batch(new_dir: str, app_exe: str, cleanup: list[str],
                    staging_dir: str | None = None) -> str:
    """Batch that copies a freshly unpacked portable build over this one.

    A one-dir build is a tree, not a single exe, so the swap is a copy rather
    than a move.  Deliberately copies *over* the old install instead of
    replacing it: robocopy failing part-way leaves an install that still runs,
    where a delete-then-copy would leave nothing.  The relaunch and the cleanup
    run whatever robocopy did, so a failed update is a no-op, not a brick.
    """
    assert new_dir and app_exe, "paths must be non-empty"
    app_dir = os.path.dirname(app_exe)
    assert app_dir, "app_exe must include its folder"
    lines = [
        _WAIT_FOR_EXIT,
        f'robocopy "{new_dir}" "{app_dir}" /E /IS /IT /R:2 /W:2 '
        "/NFL /NDL /NJH /NJS /NP >nul\r\n",
        f'start "" "{app_exe}"\r\n',
        # The staging tree, not just the copied-from folder inside it.
        f'rd /s /q "{staging_dir or new_dir}"\r\n',
    ]
    lines += [f'del /q "{path}"\r\n' for path in cleanup]
    lines.append('del /q "%~f0"\r\n')
    return "".join(lines)


def extract_portable(zip_path: str, dest_dir: str) -> str:
    """Unpack a portable update and return the folder holding ``MDBoss.exe``.

    Accepts the exe at the zip root or inside a single top-level folder, so a
    zip built either way installs.  Raises ValueError when there is no exe --
    better than copying a junk tree over a working install.
    """
    assert zip_path, "zip_path must be non-empty"
    assert dest_dir, "dest_dir must be non-empty"
    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
        assert len(names) < 100000, "update zip is implausibly large"
        member = next(
            (n for n in names
             if n.lower().rsplit("/", 1)[-1] == "mdboss.exe"), None
        )
        if member is None:
            raise ValueError("no MDBoss.exe inside the update zip")
        archive.extractall(dest_dir)
    return os.path.dirname(os.path.join(dest_dir, *member.split("/")))


def _norm(path: str) -> str:
    """Case/format-normalised absolute path, for use as a dict key."""
    return os.path.normcase(os.path.abspath(path))


def push_recent(recents: list[str], path: str,
                limit: int = MAX_RECENTS) -> list[str]:
    """``recents`` with ``path`` moved to the front, deduped and capped.

    Most-recently-viewed first.  Re-opening a document already in the list
    promotes it rather than adding a second entry; the oldest falls off the
    end once the list is full.  Pure: the caller persists the result.
    """
    assert isinstance(recents, list), "recents must be a list"
    assert path, "path must be non-empty"
    assert limit > 0, "limit must be positive"
    want = _norm(path)
    kept = [p for p in recents if _norm(p) != want]
    return [os.path.abspath(path), *kept[:limit - 1]]


def sanitize_paths(raw: object, limit: int) -> list[str]:
    """The first ``limit`` strings in ``raw``, or ``[]`` if it isn't a list.

    Config files are user-editable, so a stored path list may be missing,
    the wrong type, or hold non-strings.
    """
    assert limit > 0, "limit must be positive"
    if not isinstance(raw, list):
        return []
    return [p for p in raw if isinstance(p, str)][:limit]


def md_counts_for_root(root_path: str) -> dict[str, int]:
    """Recursive Markdown-file count for every folder under ``root_path``.

    Returns a map of normalised folder path -> number of Markdown files
    anywhere beneath (and directly in) it.  A single bottom-up ``os.walk``
    lets each folder sum its own files plus its children's totals.
    """
    assert os.path.isdir(root_path), "root must be a directory"
    counts: dict[str, int] = {}
    bound = 0
    for dirpath, dirnames, filenames in os.walk(root_path, topdown=False):
        bound += 1
        if bound > 100000:                     # bounded (Rule of 10)
            break
        total = sum(1 for name in filenames if is_markdown(name))
        for sub in dirnames:
            total += counts.get(_norm(os.path.join(dirpath, sub)), 0)
        counts[_norm(dirpath)] = total
    return counts


# --------------------------------------------------------------------------- #
# Windows file associations.
#
# Registration is per-user (HKEY_CURRENT_USER) whatever the install scope, so
# no admin rights are needed; the installer's [Run] entry uses
# runasoriginaluser so an elevated per-machine install still writes the real
# user's hive.  Note what this can and cannot do: since
# Windows 8 the UserChoice key is hash-protected, so no application can make
# itself the default handler.  These entries put MD Boss in the "Open with"
# list and in Settings > Default apps; choosing it there is the user's step.
#
# The registry layout is a pure plan so it can be tested without touching the
# registry, and so register/unregister cannot drift apart.
# --------------------------------------------------------------------------- #
PROGID = "MDBoss.Markdown"                 # our handler's ProgID
PROGID_LABEL = "Markdown Document"
CAPABILITIES_SUBKEY = rf"Software\{APP_NAME}\Capabilities"
REGISTER_FLAG = "--register-file-types"
UNREGISTER_FLAG = "--unregister-file-types"


class RegPlan(TypedDict):
    """What to write to register, and what to undo to unregister."""

    values: list[tuple[str, str, str]]      # key, value name, data
    owned_keys: list[str]                   # deleted whole, deepest first
    shared_values: list[tuple[str, str]]    # key, value name -- value only


def registration_plan(command: str, icon: str, exe_name: str) -> RegPlan:
    """The complete HKCU registration for MD Boss as a Markdown handler.

    ``command`` is the shell open command including its ``"%1"`` placeholder.
    Keys we create outright are listed for wholesale removal, deepest first so
    deletion never hits a key that still has children.  Keys shared with other
    applications -- the per-extension ``OpenWithProgids`` lists and
    ``RegisteredApplications`` -- give up only our own value.
    """
    assert "%1" in command, "command must pass the file through as %1"
    assert icon, "icon must be non-empty"
    assert exe_name, "exe_name must be non-empty"
    progid = rf"Software\Classes\{PROGID}"
    appkey = rf"Software\Classes\Applications\{exe_name}"
    values: list[tuple[str, str, str]] = [
        (progid, "", PROGID_LABEL),
        (progid, "FriendlyTypeName", PROGID_LABEL),
        (rf"{progid}\DefaultIcon", "", icon),
        (rf"{progid}\shell\open", "FriendlyAppName", DISPLAY_NAME),
        (rf"{progid}\shell\open\command", "", command),
        # Applications\<exe> is what populates the "Open with" list.
        (rf"{appkey}\shell\open\command", "", command),
        (rf"{appkey}", "FriendlyAppName", DISPLAY_NAME),
        # Capabilities + RegisteredApplications list us in Default apps.
        (CAPABILITIES_SUBKEY, "ApplicationName", DISPLAY_NAME),
        (CAPABILITIES_SUBKEY, "ApplicationDescription",
         "Local Markdown manager, editor, and offline GitHub-style viewer."),
        (r"Software\RegisteredApplications", DISPLAY_NAME,
         CAPABILITIES_SUBKEY),
    ]
    shared_values: list[tuple[str, str]] = [
        (r"Software\RegisteredApplications", DISPLAY_NAME),
    ]
    for ext in MARKDOWN_EXTS:
        values.append((rf"Software\Classes\{ext}\OpenWithProgids", PROGID, ""))
        values.append((rf"{appkey}\SupportedTypes", ext, ""))
        values.append((rf"{CAPABILITIES_SUBKEY}\FileAssociations", ext, PROGID))
        shared_values.append(
            (rf"Software\Classes\{ext}\OpenWithProgids", PROGID)
        )
    owned_keys = [
        rf"{progid}\shell\open\command",
        rf"{progid}\shell\open",
        rf"{progid}\shell",
        rf"{progid}\DefaultIcon",
        progid,
        rf"{appkey}\shell\open\command",
        rf"{appkey}\shell\open",
        rf"{appkey}\shell",
        rf"{appkey}\SupportedTypes",
        appkey,
        rf"{CAPABILITIES_SUBKEY}\FileAssociations",
        CAPABILITIES_SUBKEY,
        rf"Software\{APP_NAME}",
    ]
    return {"values": values, "owned_keys": owned_keys,
            "shared_values": shared_values}


def handler_command() -> str:
    """The shell open command for this build of MD Boss.

    Frozen, that is the exe itself; from source it is the interpreter plus
    ``app.py``, so a developer install registers something that actually runs.
    """
    if getattr(sys, "frozen", False):
        parts = [sys.executable]
    else:
        parts = [sys.executable, os.path.abspath(__file__)]
    return " ".join(f'"{part}"' for part in parts) + ' "%1"'


def current_registration_plan() -> RegPlan:
    """``registration_plan`` filled in for the running build."""
    if getattr(sys, "frozen", False):
        icon = f"{sys.executable},0"
        exe_name = os.path.basename(sys.executable)
    else:
        icon = resource_path("mdboss.ico")
        exe_name = "MDBoss.exe"
    return registration_plan(handler_command(), icon, exe_name)


def apply_registration(plan: RegPlan) -> None:
    """Write every value in ``plan`` under HKEY_CURRENT_USER."""
    assert plan["values"], "plan must have values to write"
    for key, name, data in plan["values"]:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as handle:
            winreg.SetValueEx(handle, name, 0, winreg.REG_SZ, data)


def remove_registration(plan: RegPlan) -> None:
    """Undo ``plan``.  Anything already gone is not an error."""
    for key, name in plan["shared_values"]:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, 0,
                                winreg.KEY_SET_VALUE) as handle:
                winreg.DeleteValue(handle, name)
        except OSError:
            pass
    for key in plan["owned_keys"]:              # deepest first
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key)
        except OSError:
            pass


def is_registered(command: str) -> bool:
    """True when our ProgID's open command is exactly ``command``.

    A mismatch means a stale registration -- the app was moved, or a portable
    copy elsewhere owns the ProgID -- which the UI offers to re-point.
    """
    assert command, "command must be non-empty"
    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            rf"Software\Classes\{PROGID}\shell\open\command",
        ) as handle:
            value, _kind = winreg.QueryValueEx(handle, "")
    except OSError:
        return False
    return isinstance(value, str) and value == command


def notify_assoc_changed() -> None:
    """Tell Explorer the association table changed, so it updates now."""
    if not sys.platform.startswith("win"):
        return
    shcne_assocchanged = 0x08000000
    shcnf_idlist = 0x0000
    try:
        ctypes.windll.shell32.SHChangeNotify(
            shcne_assocchanged, shcnf_idlist, None, None
        )
    except (AttributeError, OSError):
        pass                        # cosmetic only; a re-login also refreshes


def _b64(data: QByteArray) -> str:
    """Base64-encode a QByteArray to an ASCII str for JSON config."""
    return bytes(data.toBase64().data()).decode("ascii")


def templates_dir() -> str:
    """Per-user folder holding new-file templates (``*.md``)."""
    base = _user_data_base()
    return os.path.join(base, APP_NAME, "templates")


# Starter templates written on first run.  Support {{title}}, {{date}},
# {{time}}, {{datetime}} placeholders (see apply_template).
_STARTER_TEMPLATES: dict[str, str] = {
    "Meeting Notes": (
        "# {{title}}\n\n"
        "- **Date:** {{date}}\n"
        "- **Attendees:** \n\n"
        "## Agenda\n\n"
        "## Notes\n\n"
        "## Action items\n\n"
        "- [ ] \n"
    ),
    "Document": (
        "---\n"
        "title: {{title}}\n"
        "date: {{date}}\n"
        "---\n\n"
        "# {{title}}\n\n"
    ),
}


def seed_templates() -> None:
    """On first run, create the templates folder with a couple of starters."""
    directory = templates_dir()
    if os.path.exists(directory):
        return
    try:
        os.makedirs(directory, exist_ok=True)
        for name, body in _STARTER_TEMPLATES.items():
            with open(os.path.join(directory, f"{name}.md"), "w",
                      encoding="utf-8") as fh:
                fh.write(body)
    except OSError:
        pass                                  # templates are a convenience


def list_templates() -> list[tuple[str, str]]:
    """Return ``(name, path)`` for each Markdown template, sorted by name."""
    try:
        entries = sorted(os.scandir(templates_dir()),
                         key=lambda e: e.name.lower())
    except OSError:
        return []
    result: list[tuple[str, str]] = []
    count = 0
    for entry in entries:
        count += 1
        if count > 1000:                      # bounded (Rule of 10)
            break
        if entry.is_file() and is_markdown(entry.name):
            result.append((os.path.splitext(entry.name)[0], entry.path))
    return result


def apply_template(text: str, title: str) -> str:
    """Substitute {{title}}, {{date}}, {{time}}, {{datetime}} placeholders."""
    assert isinstance(text, str), "template text must be str"
    now = datetime.datetime.now()
    return (text.replace("{{title}}", title)
                .replace("{{date}}", now.strftime("%Y-%m-%d"))
                .replace("{{time}}", now.strftime("%H:%M"))
                .replace("{{datetime}}", now.strftime("%Y-%m-%d %H:%M")))


# --------------------------------------------------------------------------- #
# Web view: network lock + external-link handling.
# --------------------------------------------------------------------------- #
class LocalOnlyInterceptor(QWebEngineUrlRequestInterceptor):
    """Block every request whose scheme is not local (ITAR safeguard).

    Local documents may contain remote ``http(s)`` images or scripts; those
    requests are dropped so no document content can ever leave the machine.
    """

    _ALLOWED = frozenset({"file", "data", "qrc", "about", "blob"})

    def interceptRequest(  # noqa: N802 (Qt override)
        self, info: QWebEngineUrlRequestInfo
    ) -> None:
        scheme = info.requestUrl().scheme().lower()
        if scheme not in self._ALLOWED:
            info.block(True)


class ScrollBridge(QObject):
    """JS-exposed object: the preview reports its scroll ratio through here.

    Registered on the preview's QWebChannel as ``mdbossBridge``; the page's
    scroll handler calls ``previewScrolled(ratio)``, which is re-emitted as a
    Qt signal the main window connects to.
    """

    scrolled = Signal(float)

    @Slot(float)
    def previewScrolled(self, ratio: float) -> None:  # noqa: N802 (JS name)
        self.scrolled.emit(ratio)


class PreviewPage(QWebEnginePage):
    """Preview page that opens external links in the system browser."""

    def acceptNavigationRequest(  # noqa: N802 (Qt override)
        self, url: QUrl | str, nav_type: QWebEnginePage.NavigationType,
        is_main_frame: bool,
    ) -> bool:
        target = QUrl(url) if isinstance(url, str) else url
        clicked = QWebEnginePage.NavigationType.NavigationTypeLinkClicked
        if (nav_type == clicked
                and target.scheme() in ("http", "https", "mailto")):
            webbrowser.open(target.toString())
            return False
        return super().acceptNavigationRequest(url, nav_type, is_main_frame)


# --------------------------------------------------------------------------- #
# Source editor with a line-number gutter and current-line highlight.
# --------------------------------------------------------------------------- #
class _LineNumberArea(QWidget):
    """Thin gutter widget delegating paint/size back to its editor."""

    def __init__(self, editor: CodeEditor) -> None:
        super().__init__(editor)
        self._editor = editor

    def sizeHint(self) -> QSize:  # noqa: N802 (Qt override)
        return QSize(self._editor.gutter_width(), 0)

    def paintEvent(  # noqa: N802 (Qt override)
        self, event: QPaintEvent
    ) -> None:
        self._editor.paint_gutter(event)


class CodeEditor(QPlainTextEdit):
    """A monospace Markdown source editor with line numbers."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._gutter = _LineNumberArea(self)
        self.setFont(QFont("Consolas", 11))
        self.setTabStopDistance(self.fontMetrics().horizontalAdvance(" ") * 4)
        self.blockCountChanged.connect(lambda _n: self._refresh_margin())
        self.updateRequest.connect(self._on_update_request)
        self.cursorPositionChanged.connect(self._highlight_current_line)
        self._refresh_margin()
        self._highlight_current_line()

    def gutter_width(self) -> int:
        digits = max(2, len(str(max(1, self.blockCount()))))
        return 12 + self.fontMetrics().horizontalAdvance("9") * digits

    def _refresh_margin(self) -> None:
        self.setViewportMargins(self.gutter_width(), 0, 0, 0)

    def _on_update_request(self, rect: QRect, dy: int) -> None:
        if dy != 0:
            self._gutter.scroll(0, dy)
        else:
            self._gutter.update(
                0, rect.y(), self._gutter.width(), rect.height()
            )
        if rect.contains(self.viewport().rect()):
            self._refresh_margin()

    def resizeEvent(  # noqa: N802 (Qt override)
        self, event: QResizeEvent
    ) -> None:
        super().resizeEvent(event)
        cr = self.contentsRect()
        self._gutter.setGeometry(
            QRect(cr.left(), cr.top(), self.gutter_width(), cr.height())
        )

    # External file drops open the document at window level, rather than
    # pasting a path into the text -- ignore them so they bubble to the parent.
    def dragEnterEvent(  # noqa: N802 (Qt override)
        self, event: QDragEnterEvent
    ) -> None:
        if event.mimeData().hasUrls():
            event.ignore()
            return
        super().dragEnterEvent(event)

    def dragMoveEvent(  # noqa: N802 (Qt override)
        self, event: QDragMoveEvent
    ) -> None:
        if event.mimeData().hasUrls():
            event.ignore()
            return
        super().dragMoveEvent(event)

    def dropEvent(  # noqa: N802 (Qt override)
        self, event: QDropEvent
    ) -> None:
        if event.mimeData().hasUrls():
            event.ignore()
            return
        super().dropEvent(event)

    def _highlight_current_line(self) -> None:
        selection = QTextEdit.ExtraSelection()
        fmt = QTextCharFormat()
        fmt.setBackground(QColor("#f6f8fa"))
        fmt.setProperty(QTextFormat.Property.FullWidthSelection, True)
        selection.format = fmt
        cursor = self.textCursor()
        cursor.clearSelection()
        selection.cursor = cursor
        self.setExtraSelections([selection])

    def paint_gutter(self, event: QPaintEvent) -> None:
        painter = QPainter(self._gutter)
        painter.fillRect(event.rect(), QColor("#f6f8fa"))
        block = self.firstVisibleBlock()
        top = round(
            self.blockBoundingGeometry(block)
            .translated(self.contentOffset())
            .top()
        )
        bottom = top + round(self.blockBoundingRect(block).height())
        width = self._gutter.width() - 6
        height = self.fontMetrics().height()
        painter.setPen(QColor("#9aa0a6"))
        guard = 0
        while block.isValid() and top <= event.rect().bottom():
            guard += 1
            if guard > 100000:                 # bounded loop (Rule of 10)
                break
            if block.isVisible() and bottom >= event.rect().top():
                painter.drawText(
                    0, top, width, height,
                    int(Qt.AlignmentFlag.AlignRight),
                    str(block.blockNumber() + 1),
                )
            block = block.next()
            top = bottom
            bottom = top + round(self.blockBoundingRect(block).height())


# --------------------------------------------------------------------------- #
# Manage-folders dialog (add/remove/reorder up to five roots).
# --------------------------------------------------------------------------- #
class ManageFoldersDialog(QDialog):
    """Edit the list of root folders (max five, each an existing directory)."""

    def __init__(self, roots: list[Root], parent: QWidget | None = None):
        super().__init__(parent)
        self.setWindowTitle("Manage folders")
        self.resize(560, 320)
        self._roots = [dict(r) for r in roots][:MAX_ROOTS]

        self._list = QListWidget(self)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(f"Root folders (up to {MAX_ROOTS}):"))
        layout.addWidget(self._list, 1)

        buttons = QHBoxLayout()
        for text, slot in (
            ("Add…", self._add),
            ("Rename…", self._rename),
            ("Remove", self._remove),
            ("Up", lambda: self._move(-1)),
            ("Down", lambda: self._move(1)),
        ):
            btn = QPushButton(text, self)
            btn.clicked.connect(slot)
            buttons.addWidget(btn)
        buttons.addStretch(1)
        layout.addLayout(buttons)

        box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel
        )
        box.accepted.connect(self.accept)
        box.rejected.connect(self.reject)
        layout.addWidget(box)
        self._reload()

    def _reload(self) -> None:
        self._list.clear()
        for root in self._roots:
            item = QListWidgetItem(f"{root['name']}  —  {root['path']}")
            self._list.addItem(item)

    def _add(self) -> None:
        if len(self._roots) >= MAX_ROOTS:
            QMessageBox.information(
                self, DISPLAY_NAME, f"At most {MAX_ROOTS} roots are allowed."
            )
            return
        path = QFileDialog.getExistingDirectory(self, "Choose a root folder")
        if not path or not os.path.isdir(path):
            return
        name, ok = QInputDialog.getText(
            self, "Root name", "Display name:", text=os.path.basename(path)
        )
        if not ok:
            return
        self._roots.append({"name": name.strip() or os.path.basename(path),
                            "path": os.path.abspath(path)})
        self._reload()

    def _rename(self) -> None:
        row = self._list.currentRow()
        if row < 0:
            return
        name, ok = QInputDialog.getText(
            self, "Rename root", "Display name:", text=self._roots[row]["name"]
        )
        if ok and name.strip():
            self._roots[row]["name"] = name.strip()
            self._reload()

    def _remove(self) -> None:
        row = self._list.currentRow()
        if row >= 0:
            del self._roots[row]
            self._reload()

    def _move(self, delta: int) -> None:
        row = self._list.currentRow()
        target = row + delta
        if row < 0 or not 0 <= target < len(self._roots):
            return
        self._roots[row], self._roots[target] = (
            self._roots[target], self._roots[row]
        )
        self._list.setCurrentRow(target)
        self._reload()

    def roots(self) -> list[Root]:
        """The edited root list (name/path dicts, at most five)."""
        return self._roots[:MAX_ROOTS]


# --------------------------------------------------------------------------- #
# Background update checker (thread never touches Qt widgets -- signal only).
# --------------------------------------------------------------------------- #
class UpdateChecker(QObject):
    """Fetches the latest release off-thread and reports via signals."""

    ready = Signal(dict)
    failed = Signal(str)

    def start(self) -> None:
        thread = threading.Thread(target=self._run, daemon=True)
        thread.start()

    def _run(self) -> None:
        try:
            info = fetch_latest_release()
        except (OSError, ValueError) as exc:
            self.failed.emit(str(exc))
            return
        self.ready.emit(info)


class Downloader(QObject):
    """Downloads a URL to ``dest`` off-thread and reports via signals."""

    done = Signal(str)      # emits the completed destination path
    failed = Signal(str)

    def __init__(self, url: str, dest: str,
                 parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._url = url
        self._dest = dest

    def start(self) -> None:
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self) -> None:
        part = self._dest + ".part"
        try:
            req = urllib.request.Request(
                self._url, headers={"User-Agent": APP_NAME}
            )
            with urllib.request.urlopen(req, timeout=30) as resp, \
                    open(part, "wb") as out:
                shutil.copyfileobj(resp, out, 256 * 1024)
            os.replace(part, self._dest)
        except (OSError, ValueError) as exc:
            self.failed.emit(str(exc))
            return
        self.done.emit(self._dest)


# --------------------------------------------------------------------------- #
# Main window.
# --------------------------------------------------------------------------- #
class MainWindow(QMainWindow):
    """The MDBoss main window: tree | outline+favorites | editor | preview."""

    def __init__(self) -> None:
        super().__init__()
        cfg = load_config()

        # --- Persistent + transient state, declared up front. ---
        self._roots: list[Root] = self._sanitize_roots(cfg.get("roots", []))
        self._favorites: list[str] = sanitize_paths(
            cfg.get("favorites", []), MAX_FAVORITES
        )
        # Documents viewed, most recent first.  Written on every open.
        self._recents: list[str] = sanitize_paths(
            cfg.get("recents", []), MAX_RECENTS
        )
        self._current_path: str | None = None   # open document, or None
        self._suppress_dirty = False             # True while loading a file
        self._watched_roots: set[str] = set()
        # Path of the MD_Inbox drop folder, or None.  Recomputed on every tree
        # reload; gates whether dragged-in Markdown files are accepted.
        self._inbox_path: str | None = None
        # Recursive Markdown counts per folder, shown beside folder names.
        self._md_counts: dict[str, int] = {}
        self._init_pane_flags(cfg)
        # Bidirectional scroll-sync echo guards (one flag per direction).
        self._suppress_from_editor = False
        self._suppress_from_preview = False

        # --- Web view network lock (asset URLs resolved by mdrender). ---
        self._interceptor = LocalOnlyInterceptor(self)
        profile = QWebEngineProfile.defaultProfile()
        profile.setUrlRequestInterceptor(self._interceptor)

        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(PREVIEW_DEBOUNCE_MS)
        self._debounce.timeout.connect(self._render_preview)

        self._updater = UpdateChecker(self)
        self._updater.ready.connect(self._on_update_ready)
        self._updater.failed.connect(self._on_update_failed)
        self._update_manual = False
        # Auto-update download state.
        self._downloader: Downloader | None = None
        self._dl_dialog: QProgressDialog | None = None
        self._dl_portable = False
        self._appimage_target: str | None = None  # AppImage self-update target
        self._updating = False                   # skip close-time re-ask

        self.setWindowTitle(f"{DISPLAY_NAME} - v{APP_VERSION}")
        self.setAcceptDrops(True)                # dropped documents open here
        # An app-wide filter also catches drops over the preview's native web
        # widget, which does not forward them to the window on its own.  On
        # Linux/macOS an application-wide filter makes PySide try to wrap every
        # QObject QtWebEngine posts events for (including internal ones whose
        # lifetime it does not own), which segfaults; there we install a
        # narrow filter on the preview widget instead (see _build_panes).
        instance = QApplication.instance()
        if instance is not None and sys.platform.startswith("win"):
            instance.installEventFilter(self)
        self.resize(1280, 800)
        icon = resource_path("mdboss.ico")
        if os.path.isfile(icon):
            self.setWindowIcon(QIcon(icon))

        self._build_toolbar()
        self._build_panes()
        self._apply_pane_toggles()
        self._start_ipc_server()
        self._restore_geometry(cfg)
        self._reload_tree()
        self._reload_favorites()
        self._reload_recents()
        self._render_preview()

        # The in-app updater installs a Windows .exe, so it only runs there.
        if cfg.get("check_updates", True) and (
                sys.platform.startswith("win") or running_appimage()):
            QTimer.singleShot(2000, self._start_update_check)

    def _init_pane_flags(self, cfg: ConfigDict) -> None:
        """Saved preview/pane toggle states, read before the UI is built."""
        # Hide a leading YAML front-matter block in the preview (default on).
        self._init_hide_yaml = bool(cfg.get("hide_front_matter", True))
        # Which panes are visible (the Files | Outline | Edit toggles),
        # remembered across runs.  Unprefixed keys are this app's; the port
        # keeps its own pane state under wx_show_* because visibility there
        # is tied to its own splitters.
        self._init_show_tree = bool(cfg.get("show_files", True))
        self._init_show_outline = bool(cfg.get("show_outline", True))
        self._init_show_editor = bool(cfg.get("show_editor", True))

    def _apply_pane_toggles(self) -> None:
        """Re-apply the saved pane toggles once the actions and the panes both
        exist -- and before the splitter states restore, so sizes saved with a
        pane hidden are applied to the same arrangement."""
        self._toggle_tree()
        self._toggle_outline()
        self._toggle_editor()

    # ---- UI construction ------------------------------------------------- #
    def _build_toolbar(self) -> None:
        bar = QToolBar("Main", self)
        bar.setMovable(False)
        self.addToolBar(bar)

        def add(text: str, slot: Callable[[], None],
                shortcut: str | None = None, tip: str = "") -> QAction:
            action = QAction(text, self)
            action.triggered.connect(slot)
            if shortcut:
                action.setShortcut(QKeySequence(shortcut))
            action.setToolTip(tip or text)
            bar.addAction(action)
            return action

        add("Manage folders…", self._manage_folders,
            tip="Add, remove, or reorder root folders")
        add("Refresh", self._reload_tree, "F5", "Rescan all roots")
        bar.addSeparator()
        add("Open…", self._open_dialog, "Ctrl+O",
            "Open a Markdown file from anywhere on disk")
        add("New", self._new_file, "Ctrl+N", "Create a new Markdown file")
        add("New from template…", self._new_from_template_dialog,
            tip="Create a new file from a template")
        add("Save", self._save_file, "Ctrl+S", "Save the current document")
        bar.addSeparator()
        # Toggles ordered to match the columns: Files | Outline | Edit.
        self._act_tree = add("Files", self._toggle_tree,
                             tip="Show or hide the file tree")
        self._act_tree.setCheckable(True)
        self._act_tree.setChecked(self._init_show_tree)
        self._act_outline = add("Outline", self._toggle_outline,
                                tip="Show or hide the outline pane")
        self._act_outline.setCheckable(True)
        self._act_outline.setChecked(self._init_show_outline)
        self._act_editor = add("Edit", self._toggle_editor,
                               tip="Show or hide the source editor")
        self._act_editor.setCheckable(True)
        self._act_editor.setChecked(self._init_show_editor)
        self._act_yaml = add(
            "Hide YAML", self._toggle_yaml,
            tip="Hide a YAML front-matter block at the top of the file",
        )
        self._act_yaml.setCheckable(True)
        self._act_yaml.setChecked(self._init_hide_yaml)
        bar.addSeparator()
        # Windows only: on Linux the .desktop entry written by
        # install-linux.sh already declares MimeType=text/markdown.
        if sys.platform.startswith("win"):
            add("File types…", self._file_types_dialog,
                tip="Register MD Boss as a handler for Markdown files")
        add("Help", self._show_help, "F1", "About MDBoss")

    def _panel_header(
        self, title: str, tip: str,
        actions: list[tuple[str, Callable[[], None]] | None],
    ) -> QHBoxLayout:
        """A ``title`` row with a ``⋯`` menu button.  ``None`` = separator."""
        assert title, "title must be non-empty"
        assert actions, "actions must be non-empty"
        row = QHBoxLayout()
        row.addWidget(QLabel(title))
        row.addStretch(1)
        button = QToolButton(self)
        button.setText("⋯")
        button.setToolTip(tip)
        button.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        menu = QMenu(button)
        for action in actions:
            if action is None:
                menu.addSeparator()
            else:
                menu.addAction(action[0], action[1])
        button.setMenu(menu)
        row.addWidget(button)
        return row

    def _build_panes(self) -> None:
        """Assemble the three columns: files | outline | editor + preview."""
        left = self._build_left_pane()
        outline_box = self._build_outline_pane()
        self._right = self._build_editor_preview_pane()
        # Minimum widths so no column can be dragged or restored to zero.
        left.setMinimumWidth(160)
        outline_box.setMinimumWidth(150)
        self._main_split = QSplitter(Qt.Orientation.Horizontal, self)
        for pane in (left, outline_box, self._right):
            self._main_split.addWidget(pane)
        self._main_split.setStretchFactor(2, 1)
        self._main_split.setChildrenCollapsible(False)
        self._main_split.setSizes([300, 200, 780])
        self._left = left
        self.setCentralWidget(self._main_split)

    def _build_list_panel(
        self, title: str, tip: str,
        actions: list[tuple[str, Callable[[], None]] | None],
        on_activate: Callable[[QListWidgetItem], None],
        on_menu: Callable[[QPoint], None],
    ) -> tuple[QWidget, QListWidget]:
        """A titled panel wrapping a click-to-open list of documents.

        Recent and Favorites differ only in their title, header menu and
        handlers, so they share this.  Returns the panel and its list, since
        the caller keeps the list to refill it later.
        """
        assert title, "title must be non-empty"
        box = QWidget(self)
        layout = QVBoxLayout(box)
        layout.setContentsMargins(4, 4, 4, 2)
        layout.addLayout(self._panel_header(title, tip, actions))
        listing = QListWidget(self)
        listing.itemActivated.connect(on_activate)
        listing.itemClicked.connect(on_activate)
        listing.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        listing.customContextMenuRequested.connect(on_menu)
        layout.addWidget(listing, 1)
        box.setMinimumHeight(56)
        return box, listing

    def _build_left_pane(self) -> QSplitter:
        """Recent over Favorites over the file tree, in a vertical splitter so
        each area can be dragged taller or shorter."""
        left = QSplitter(Qt.Orientation.Vertical, self)
        recent_box, self._recent_list = self._build_list_panel(
            "Recent", "Clear the recent documents list",
            [("Clear recent documents", self._clear_recents)],
            self._on_recent_activated, self._recent_menu,
        )
        fav_box, self._fav_list = self._build_list_panel(
            "Favorites", "Clear, export, or import your favorites",
            [("Export favorites…", self._export_favorites),
             ("Import favorites…", self._import_favorites),
             None,
             ("Clear all favorites", self._clear_favorites)],
            self._on_favorite_activated, self._fav_menu,
        )
        files_box = self._build_files_box()
        for box in (recent_box, fav_box, files_box):
            left.addWidget(box)
        left.setChildrenCollapsible(False)
        left.setStretchFactor(2, 1)          # the file list takes extra space
        left.setSizes([130, 150, 460])
        return left

    def _build_files_box(self) -> QWidget:
        """The root-folder tree, with its filter box above it."""
        files_box = QWidget(self)
        files_layout = QVBoxLayout(files_box)
        files_layout.setContentsMargins(4, 2, 4, 4)
        files_layout.addWidget(QLabel("Files"))
        filter_row = QHBoxLayout()
        filter_row.addWidget(QLabel("\U0001F50D"))
        self._filter = QLineEdit(self)
        self._filter.setPlaceholderText("Filter files…")
        self._filter.textChanged.connect(self._apply_filter)
        filter_row.addWidget(self._filter, 1)
        clear = QPushButton("✕", self)
        clear.setFixedWidth(28)
        clear.clicked.connect(self._filter.clear)
        filter_row.addWidget(clear)
        files_layout.addLayout(filter_row)
        self._tree = QTreeWidget(self)
        self._tree.setHeaderHidden(True)
        self._tree.setContextMenuPolicy(
            Qt.ContextMenuPolicy.CustomContextMenu
        )
        self._tree.customContextMenuRequested.connect(self._tree_menu)
        self._tree.itemExpanded.connect(self._on_item_expanded)
        self._tree.itemActivated.connect(self._on_item_activated)
        self._tree.itemClicked.connect(self._on_item_activated)
        files_layout.addWidget(self._tree, 1)
        files_box.setMinimumHeight(120)
        return files_box

    def _build_outline_pane(self) -> QWidget:
        """Headings of the current document; a click scrolls the preview."""
        outline_box = QWidget(self)
        ob_layout = QVBoxLayout(outline_box)
        ob_layout.setContentsMargins(4, 4, 4, 4)
        ob_layout.addWidget(QLabel("Outline"))
        self._outline = QListWidget(self)
        self._outline.itemActivated.connect(self._on_outline_activated)
        self._outline.itemClicked.connect(self._on_outline_activated)
        ob_layout.addWidget(self._outline, 1)
        self._mid = outline_box
        return outline_box

    def _build_editor_preview_pane(self) -> QSplitter:
        """The source editor beside the live preview."""
        self._editor = CodeEditor(self)
        self._editor.setPlaceholderText(
            "Select a Markdown file, or press Ctrl+N to create one."
        )
        self._editor.textChanged.connect(self._on_text_changed)
        self._editor.document().modificationChanged.connect(
            self._on_modified_changed
        )
        self._editor.verticalScrollBar().valueChanged.connect(
            self._sync_preview_scroll
        )
        self._preview = QWebEngineView(self)
        self._preview.setAcceptDrops(False)      # the window handles drops
        self._preview.setPage(PreviewPage(self._preview))
        self._watch_preview_drops()
        self._connect_preview_bridge()
        self._lock_preview_settings()
        right = QSplitter(Qt.Orientation.Horizontal, self)
        right.addWidget(self._editor)
        right.addWidget(self._preview)
        right.setSizes([500, 700])
        right.setChildrenCollapsible(False)
        return right

    def _watch_preview_drops(self) -> None:
        """Make drops over the preview reach the window on non-Windows.

        An app-wide event filter crashes there (see __init__), so watch just
        the preview and its lazily-created native child instead.
        """
        if sys.platform.startswith("win"):
            return
        self._preview.installEventFilter(self)

        def _watch_preview_child() -> None:
            child = self._preview.focusProxy()
            if child is not None:
                child.setAcceptDrops(True)
                child.installEventFilter(self)

        QTimer.singleShot(0, _watch_preview_child)
        self._preview.loadFinished.connect(lambda _ok: _watch_preview_child())

    def _connect_preview_bridge(self) -> None:
        """Web channel: the page reports its own scroll position back to Qt for
        preview -> editor sync (editor -> preview is driven from Qt)."""
        self._bridge = ScrollBridge(self)
        self._bridge.scrolled.connect(self._on_preview_scrolled)
        self._channel = QWebChannel(self)
        self._channel.registerObject("mdbossBridge", self._bridge)
        self._preview.page().setWebChannel(self._channel)
        # Re-apply the editor's scroll position after each re-render so the
        # preview does not jump to the top while typing.
        self._preview.loadFinished.connect(
            lambda _ok: self._sync_preview_scroll()
        )

    def _lock_preview_settings(self) -> None:
        """Local files may load; remote URLs and plugins may not."""
        settings = self._preview.settings()
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.LocalContentCanAccessFileUrls, True
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.LocalContentCanAccessRemoteUrls,
            False,
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.PluginsEnabled, False
        )

    # ---- Roots + tree ---------------------------------------------------- #
    @staticmethod
    def _sanitize_roots(raw: object) -> list[Root]:
        roots: list[Root] = []
        if not isinstance(raw, list):
            return roots
        for entry in raw:
            if (isinstance(entry, dict) and isinstance(entry.get("path"), str)
                    and os.path.isdir(entry["path"])):
                roots.append({
                    "name": str(entry.get("name")
                                or os.path.basename(entry["path"])),
                    "path": os.path.abspath(entry["path"]),
                })
            if len(roots) >= MAX_ROOTS:
                break
        return roots

    def _reload_tree(self) -> None:
        self._tree.clear()
        self._md_counts = {}
        for root in self._roots:
            try:
                self._md_counts.update(md_counts_for_root(root["path"]))
            except OSError:
                pass
        for root in self._roots:
            count = self._md_counts.get(_norm(root["path"]), 0)
            item = QTreeWidgetItem(self._tree, [f"{root['name']}  ({count})"])
            item.setData(0, ROLE_PATH, root["path"])
            item.setData(0, ROLE_KIND, "root")
            item.setForeground(0, QColor("#0a58ca"))
            self._add_placeholder(item)
        self._refresh_watcher()
        self._inbox_path = find_inbox(self._roots)
        if not self._roots:
            self._tree.addTopLevelItem(
                QTreeWidgetItem(["(no folders — use Manage folders…)"])
            )

    def _add_placeholder(self, item: QTreeWidgetItem) -> None:
        placeholder = QTreeWidgetItem([""])
        placeholder.setData(0, ROLE_KIND, "placeholder")
        item.addChild(placeholder)

    def _on_item_expanded(self, item: QTreeWidgetItem) -> None:
        if item.childCount() != 1:
            return
        child = item.child(0)
        if child.data(0, ROLE_KIND) != "placeholder":
            return
        item.removeChild(child)
        self._populate(item)

    def _populate(self, item: QTreeWidgetItem) -> None:
        path = item.data(0, ROLE_PATH)
        try:
            entries = sorted(
                os.scandir(path),
                key=lambda e: (not e.is_dir(), e.name.lower()),
            )
        except OSError:
            return
        count = 0
        for entry in entries:
            count += 1
            if count > 20000:                      # bounded (Rule of 10)
                break
            if entry.is_dir():
                # Hide folders with no Markdown anywhere beneath them.  Counts
                # are recursive, so a 0 means nothing is being concealed.  A
                # folder missing from the map was never walked (junction /
                # unreadable) — leave it visible rather than guess.
                sub = self._md_counts.get(_norm(entry.path))
                if sub == 0:
                    continue
                child = QTreeWidgetItem(item, [f"{entry.name}  ({sub or 0})"])
                child.setData(0, ROLE_PATH, entry.path)
                child.setData(0, ROLE_KIND, "dir")
                self._add_placeholder(child)
            elif is_markdown(entry.name):
                child = QTreeWidgetItem(item, [entry.name])
                child.setData(0, ROLE_PATH, entry.path)
                child.setData(0, ROLE_KIND, "file")

    def _on_item_activated(self, item: QTreeWidgetItem, _col: int = 0) -> None:
        if item.data(0, ROLE_KIND) == "file":
            self._open_file(item.data(0, ROLE_PATH))

    def _apply_filter(self, text: str) -> None:
        needle = text.strip().lower()
        for i in range(self._tree.topLevelItemCount()):
            item = self._tree.topLevelItem(i)
            if item is not None:
                self._filter_item(item, needle)

    def _filter_item(self, item: QTreeWidgetItem, needle: str) -> bool:
        kind = item.data(0, ROLE_KIND)
        if kind == "file":
            visible = needle in item.text(0).lower()
            item.setHidden(not visible)
            return visible
        any_visible = False
        for i in range(item.childCount()):
            if self._filter_item(item.child(i), needle):
                any_visible = True
        if kind in ("root", "dir"):
            item.setHidden(bool(needle) and not any_visible)
            if needle and any_visible:
                item.setExpanded(True)
        return any_visible or not needle

    # ---- File watching --------------------------------------------------- #
    def _refresh_watcher(self) -> None:
        # A light refresh hook; full recursive watching is intentionally
        # avoided.  Refresh (F5) rescans on demand.
        self._watched_roots = {r["path"] for r in self._roots}

    # ---- Drag-and-drop ---------------------------------------------------- #
    # A dropped Markdown file is opened where it lies, from anywhere on disk --
    # it need not be under a root folder.  Copying files into MD_Inbox is a
    # deliberate act now, on the Files context menu.
    def _accepts_drop(self, event: QDropEvent) -> bool:
        """True when the drag carries at least one Markdown file."""
        data = event.mimeData()
        if not data.hasUrls():
            return False
        return any(
            url.isLocalFile() and is_markdown(url.toLocalFile())
            for url in data.urls()
        )

    def eventFilter(  # noqa: N802 (Qt override)
        self, watched: QObject, event: QEvent
    ) -> bool:
        # App-wide catch for Markdown-file drags so a drop lands even over the
        # preview's native web widget.  Non-file drags fall through to normal
        # handling (e.g. moving text within the editor).
        if (event.type() in _DND_EVENT_TYPES
                and isinstance(event, QDropEvent)
                and self._accepts_drop(event)):
            if event.type() == QEvent.Type.Drop:
                self._perform_drop(event)
            else:
                event.acceptProposedAction()
            return True
        return super().eventFilter(watched, event)

    def dragEnterEvent(  # noqa: N802 (Qt override)
        self, event: QDragEnterEvent
    ) -> None:
        if self._accepts_drop(event):
            event.acceptProposedAction()
        else:
            event.ignore()

    def dragMoveEvent(  # noqa: N802 (Qt override)
        self, event: QDragMoveEvent
    ) -> None:
        if self._accepts_drop(event):
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event: QDropEvent) -> None:  # noqa: N802 (Qt override)
        if not self._accepts_drop(event):
            event.ignore()
            return
        self._perform_drop(event)

    def _perform_drop(self, event: QDropEvent) -> None:
        """Accept ``event`` and open the first Markdown file it carries."""
        event.acceptProposedAction()
        files = [
            url.toLocalFile() for url in event.mimeData().urls()
            if url.isLocalFile() and is_markdown(url.toLocalFile())
        ]
        if files:
            self.open_path(files[0])       # extra files stay where they are

    def _import_to_inbox_dialog(self) -> None:
        """Pick Markdown files to copy into MD_Inbox."""
        inbox = self._inbox_path
        if not inbox or not os.path.isdir(inbox):
            QMessageBox.information(
                self, DISPLAY_NAME,
                f"No {INBOX_NAME} folder was found.\n\n"
                f"Create a folder named {INBOX_NAME} inside one of your root "
                "folders (or add one as a root), then try again.",
            )
            return
        files, _ = QFileDialog.getOpenFileNames(
            self, f"Import files into {INBOX_NAME}", "", MARKDOWN_FILTER
        )
        if files:
            self._import_to_inbox(files)

    def _import_to_inbox(self, files: list[str]) -> None:
        """Copy Markdown ``files`` into MD_Inbox, refresh the tree, and open the
        first.  A file already inside the inbox is opened in place."""
        inbox = self._inbox_path
        if not inbox or not os.path.isdir(inbox):
            return
        copied: list[str] = []
        failed: list[str] = []
        for src in files[:1000]:                   # bounded (Rule of 10)
            if not os.path.isfile(src):
                continue
            if _norm(os.path.dirname(src)) == _norm(inbox):
                copied.append(src)                 # already here; don't dup
                continue
            dest = unique_dest(inbox, os.path.basename(src))
            try:
                shutil.copy2(src, dest)
            except OSError:
                failed.append(src)
                continue
            copied.append(dest)
        self._reload_tree()
        if failed:
            QMessageBox.warning(
                self, DISPLAY_NAME,
                "Could not copy these files into MD_Inbox:\n"
                + "\n".join(os.path.basename(f) for f in failed),
            )
        if copied:
            self._open_file(copied[0])

    # ---- Open / edit / save ---------------------------------------------- #
    def open_path(self, path: str) -> bool:
        """Open ``path`` from anywhere on disk, saying why if it cannot be.

        The entry point for callers outside the file tree -- the command line
        (file associations), drops, and Open file.  Unlike ``_open_file`` it
        never fails silently, because those callers have no other feedback.
        """
        assert isinstance(path, str), "path must be a string"
        if not path:
            return False
        full = os.path.abspath(path)
        if not os.path.isfile(full):
            QMessageBox.warning(
                self, DISPLAY_NAME, f"File not found:\n{full}"
            )
            return False
        if not is_markdown(full) and not self._confirm_non_markdown(full):
            return False
        return self._open_file(full)

    def _confirm_non_markdown(self, path: str) -> bool:
        """Ask before treating a file without a Markdown extension as one."""
        assert path, "path must be non-empty"
        ext = os.path.splitext(path)[1] or "no extension"
        return QMessageBox.question(
            self, DISPLAY_NAME,
            f"{os.path.basename(path)} has {ext}, which is not a Markdown "
            "file type.\n\nOpen it as Markdown anyway?",
        ) == QMessageBox.StandardButton.Yes

    def _open_dialog(self) -> None:
        """Open file: browse for a document anywhere on disk."""
        if self._current_path:
            start = os.path.dirname(self._current_path)
        elif self._roots:
            start = self._roots[0]["path"]
        else:
            start = os.path.expanduser("~")
        path, _ = QFileDialog.getOpenFileName(
            self, "Open Markdown file", start,
            f"{MARKDOWN_FILTER};;All files (*)",
        )
        if path:
            self.open_path(path)

    def _open_file(self, path: str) -> bool:
        if not path or not os.path.isfile(path):
            return False
        if not self._confirm_discard():
            return False
        try:
            # utf-8-sig, not utf-8: Notepad and most Windows editors write a
            # BOM, and a leading U+FEFF stops "# Heading" being a heading --
            # the document opens looking like plain text.  The codec is a
            # no-op on files without one.  Saving still writes plain utf-8,
            # so a round trip quietly drops the BOM rather than keeping it.
            with open(path, encoding="utf-8-sig") as fh:
                text = fh.read()
        except (OSError, ValueError) as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Cannot open file:\n{exc}"
            )
            return False
        self._suppress_dirty = True
        self._editor.setPlainText(text)
        self._editor.document().setModified(False)
        self._suppress_dirty = False
        self._current_path = path
        self._add_recent(path)
        self._render_preview()
        self._update_title()
        return True

    def _on_text_changed(self) -> None:
        if not self._suppress_dirty:
            self._debounce.start()

    def _on_modified_changed(self, _modified: bool) -> None:
        self._update_title()

    def _save_file(self) -> None:
        if self._current_path is None:
            self._save_file_as()
            return
        self._write_current(self._current_path)

    def _save_file_as(self) -> None:
        start = self._roots[0]["path"] if self._roots else os.path.expanduser(
            "~"
        )
        path, _ = QFileDialog.getSaveFileName(
            self, "Save Markdown", start, "Markdown (*.md *.markdown)"
        )
        if not path:
            return
        if os.path.splitext(path)[1] == "":
            path += ".md"
        self._current_path = path
        self._write_current(path)
        self._reload_tree()

    def _write_current(self, path: str) -> None:
        try:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(self._editor.toPlainText())
        except OSError as exc:
            QMessageBox.warning(self, DISPLAY_NAME, f"Cannot save:\n{exc}")
            return
        self._editor.document().setModified(False)
        self._update_title()

    def _confirm_discard(self) -> bool:
        if not self._editor.document().isModified():
            return True
        answer = QMessageBox.question(
            self, DISPLAY_NAME,
            "The current document has unsaved changes. Discard them?",
            QMessageBox.StandardButton.Save
            | QMessageBox.StandardButton.Discard
            | QMessageBox.StandardButton.Cancel,
        )
        if answer == QMessageBox.StandardButton.Save:
            self._save_file()
            return not self._editor.document().isModified()
        return answer == QMessageBox.StandardButton.Discard

    def _new_file(self) -> None:
        self._load_new_buffer("# New document\n\n")

    def _load_new_buffer(self, content: str) -> None:
        """Replace the editor with a fresh unsaved buffer of ``content``."""
        if not self._confirm_discard():
            return
        self._suppress_dirty = True
        self._editor.setPlainText(content)
        self._editor.document().setModified(True)
        self._suppress_dirty = False
        self._current_path = None
        self._render_preview()
        self._update_title()

    def _template_content(self, template_path: str | None, title: str) -> str:
        """Body for a new file: blank heading, or a template with placeholders
        substituted."""
        if template_path is None:
            return f"# {title}\n\n"
        try:
            # utf-8-sig: a template written in Notepad would otherwise put a
            # BOM in front of the new document's first heading.
            with open(template_path, encoding="utf-8-sig") as fh:
                text = fh.read()
        except OSError as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Cannot read template:\n{exc}"
            )
            return f"# {title}\n\n"
        return apply_template(text, title)

    def _new_from_template_dialog(self) -> None:
        """Toolbar action: pick a template and open it as a new buffer."""
        templates = list_templates()
        if not templates:
            self._no_templates_prompt()
            return
        names = [name for name, _path in templates]
        name, ok = QInputDialog.getItem(
            self, "New from template", "Template:", names, 0, False
        )
        if not ok or not name:
            return
        path = dict(templates)[name]
        self._load_new_buffer(self._template_content(path, "New document"))

    def _no_templates_prompt(self) -> None:
        answer = QMessageBox.question(
            self, DISPLAY_NAME,
            "You have no templates yet.\n\n"
            f"Templates are .md files in:\n{templates_dir()}\n\n"
            "Open that folder now?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer == QMessageBox.StandardButton.Yes:
            self._open_templates_folder()

    def _open_templates_folder(self) -> None:
        directory = templates_dir()
        try:
            os.makedirs(directory, exist_ok=True)
            if sys.platform.startswith("win"):
                subprocess.Popen(["explorer", os.path.normpath(directory)])
            else:
                webbrowser.open(QUrl.fromLocalFile(directory).toString())
        except OSError:
            pass

    def _update_title(self) -> None:
        shown = (os.path.normpath(self._current_path)
                 if self._current_path else "Untitled")
        dirty = "*" if self._editor.document().isModified() else ""
        self.setWindowTitle(f"{dirty}{shown} - {DISPLAY_NAME} v{APP_VERSION}")

    # ---- Preview + outline ---------------------------------------------- #
    def _render_preview(self) -> None:
        text = self._editor.toPlainText()
        doc_dir = (os.path.dirname(self._current_path)
                   if self._current_path else
                   (self._roots[0]["path"] if self._roots
                    else os.path.expanduser("~")))
        base = QUrl.fromLocalFile(os.path.join(doc_dir, "")).toString()
        if not base.endswith("/"):
            base += "/"
        strip_yaml = self._act_yaml.isChecked()
        html = mdrender.render_document(
            text, base,
            title=os.path.basename(self._current_path or "MDBoss"),
            strip_yaml=strip_yaml,
        )
        self._preview.setHtml(html, QUrl(base))
        self._rebuild_outline(text, strip_yaml)

    def _rebuild_outline(self, text: str, strip_yaml: bool) -> None:
        self._outline.clear()
        for level, heading, slug in mdrender.extract_outline(
            text, strip_yaml
        ):
            item = QListWidgetItem("  " * (level - 1) + heading)
            item.setData(int(Qt.ItemDataRole.UserRole), slug)
            self._outline.addItem(item)

    def _on_outline_activated(self, item: QListWidgetItem) -> None:
        slug = item.data(int(Qt.ItemDataRole.UserRole))
        if not slug:
            return
        script = (
            f"var el=document.getElementById({json.dumps(str(slug))});"
            "if(el){el.scrollIntoView({behavior:'smooth',block:'start'});}"
        )
        self._preview.page().runJavaScript(script)

    # ---- Recent documents ------------------------------------------------ #
    # The last MAX_RECENTS documents opened, newest first, maintained
    # automatically by _open_file and persisted like favorites.
    @staticmethod
    def _fill_path_list(widget: QListWidget, paths: list[str],
                        hint: str) -> None:
        """Show ``paths`` as filename-only rows, or a greyed ``hint`` if empty.

        The full path goes in the tooltip and UserRole.  A path that no longer
        resolves to a file is drawn in red rather than dropped, so a moved or
        deleted document is visible instead of silently vanishing.
        """
        assert hint, "hint must be non-empty"
        widget.clear()
        if not paths:
            item = QListWidgetItem(hint)
            item.setForeground(QColor("#999"))
            item.setFlags(Qt.ItemFlag.NoItemFlags)
            widget.addItem(item)
            return
        for path in paths:
            item = QListWidgetItem(os.path.basename(path))   # filename only
            item.setToolTip(path)                            # full path (hover)
            item.setData(int(Qt.ItemDataRole.UserRole), path)
            if not os.path.isfile(path):
                item.setForeground(QColor("#c01c28"))        # missing file
            widget.addItem(item)

    def _reload_recents(self) -> None:
        self._fill_path_list(self._recent_list, self._recents,
                             "(documents you open appear here)")

    def _on_recent_activated(self, item: QListWidgetItem) -> None:
        path = item.data(int(Qt.ItemDataRole.UserRole))
        if isinstance(path, str):
            self._open_file(path)

    def _add_recent(self, path: str) -> None:
        """Record ``path`` as the most recently viewed document."""
        self._recents = push_recent(self._recents, path)
        update_config({"recents": self._recents})
        self._reload_recents()

    def _clear_recents(self) -> None:
        if not self._recents:
            QMessageBox.information(
                self, DISPLAY_NAME, "Your recent list is already empty."
            )
            return
        self._recents = []
        update_config({"recents": self._recents})
        self._reload_recents()

    def _recent_menu(self, pos: QPoint) -> None:
        item = self._recent_list.itemAt(pos)
        path = item.data(int(Qt.ItemDataRole.UserRole)) if item else None
        menu = QMenu(self)
        if isinstance(path, str):
            menu.addAction("Open", lambda: self._open_file(path))
            fav = ("Remove from favorites" if self._is_favorite(path)
                   else "Add to favorites")
            menu.addAction(fav, lambda: self._toggle_favorite(path))
            menu.addAction("Reveal in Explorer", lambda: self._reveal(path))
            menu.addAction("Copy path",
                           lambda: QApplication.clipboard().setText(path))
            menu.addSeparator()
        menu.addAction("Clear recent documents", self._clear_recents)
        menu.exec(self._recent_list.viewport().mapToGlobal(pos))

    # ---- Favorites ------------------------------------------------------- #
    # Favorites are pinned documents, newest first, capped at MAX_FAVORITES;
    # adding an 11th drops the oldest.  Stored as absolute paths and persisted.
    def _is_favorite(self, path: str) -> bool:
        want = _norm(path)
        return any(_norm(f) == want for f in self._favorites)

    def _reload_favorites(self) -> None:
        self._fill_path_list(self._fav_list, self._favorites,
                             "(right-click a file to add)")

    def _on_favorite_activated(self, item: QListWidgetItem) -> None:
        path = item.data(int(Qt.ItemDataRole.UserRole))
        if isinstance(path, str):
            self._open_file(path)

    def _add_favorite(self, path: str) -> None:
        """Pin ``path`` to the top of the list (deduped, capped)."""
        want = _norm(path)
        self._favorites = [f for f in self._favorites if _norm(f) != want]
        self._favorites.insert(0, os.path.abspath(path))
        del self._favorites[MAX_FAVORITES:]          # drop the oldest
        update_config({"favorites": self._favorites})
        self._reload_favorites()

    def _remove_favorite(self, path: str) -> None:
        want = _norm(path)
        kept = [f for f in self._favorites if _norm(f) != want]
        if len(kept) == len(self._favorites):
            return
        self._favorites = kept
        update_config({"favorites": self._favorites})
        self._reload_favorites()

    def _toggle_favorite(self, path: str) -> None:
        if self._is_favorite(path):
            self._remove_favorite(path)
        else:
            self._add_favorite(path)

    def _fav_menu(self, pos: QPoint) -> None:
        item = self._fav_list.itemAt(pos)
        path = item.data(int(Qt.ItemDataRole.UserRole)) if item else None
        menu = QMenu(self)
        if isinstance(path, str):
            menu.addAction("Open", lambda: self._open_file(path))
            menu.addAction("Remove from favorites",
                           lambda: self._remove_favorite(path))
            menu.addAction("Reveal in Explorer",
                           lambda: self._reveal(path))
            menu.addAction("Copy path",
                           lambda: QApplication.clipboard().setText(path))
            menu.addSeparator()
        menu.addAction("Export favorites…", self._export_favorites)
        menu.addAction("Import favorites…", self._import_favorites)
        menu.addSeparator()
        menu.addAction("Clear all favorites", self._clear_favorites)
        menu.exec(self._fav_list.viewport().mapToGlobal(pos))

    def _clear_favorites(self) -> None:
        count = len(self._favorites)
        if count == 0:
            QMessageBox.information(
                self, DISPLAY_NAME, "You have no favorites."
            )
            return
        plural = "s" if count != 1 else ""
        if QMessageBox.question(
            self, DISPLAY_NAME, f"Remove all {count} favorite{plural}?",
        ) != QMessageBox.StandardButton.Yes:
            return
        self._favorites = []
        update_config({"favorites": self._favorites})
        self._reload_favorites()

    def _export_favorites(self) -> None:
        if not self._favorites:
            QMessageBox.information(
                self, DISPLAY_NAME, "You have no favorites to export."
            )
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Export favorites", "mdboss-favorites.json",
            "JSON files (*.json)",
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump({"favorites": self._favorites}, fh, indent=2)
        except OSError as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not write file:\n{exc}"
            )
            return
        QMessageBox.information(
            self, DISPLAY_NAME,
            f"Exported {len(self._favorites)} favorite(s) to:\n{path}",
        )

    def _import_favorites(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Import favorites", "", "JSON files (*.json);;All files (*)"
        )
        if not path:
            return
        imported = self._read_favorites_file(path)
        if imported is None:
            return
        merge = True
        if self._favorites:
            answer = QMessageBox.question(
                self, DISPLAY_NAME,
                "Merge with your current favorites?\n\n"
                "Yes — add the imported files to your list\n"
                "No — replace your current favorites",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
                | QMessageBox.StandardButton.Cancel,
            )
            if answer == QMessageBox.StandardButton.Cancel:
                return
            merge = answer == QMessageBox.StandardButton.Yes
        combined = (self._favorites + imported) if merge else imported
        seen: set[str] = set()
        deduped: list[str] = []
        for entry in combined:
            key = _norm(entry)
            if key not in seen:
                seen.add(key)
                deduped.append(entry)
        self._favorites = deduped[:MAX_FAVORITES]
        update_config({"favorites": self._favorites})
        self._reload_favorites()
        QMessageBox.information(
            self, DISPLAY_NAME,
            f"Your favorites list now has {len(self._favorites)} item(s).",
        )

    def _read_favorites_file(self, path: str) -> list[str] | None:
        try:
            # utf-8-sig: json.load rejects a BOM outright ("Unexpected UTF-8
            # BOM"), so a favorites file saved by a Windows editor would fail
            # to import with a message about JSON rather than about encoding.
            with open(path, encoding="utf-8-sig") as fh:
                data = json.load(fh)
        except (OSError, ValueError) as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not read file:\n{exc}"
            )
            return None
        raw = data.get("favorites") if isinstance(data, dict) else data
        if not isinstance(raw, list):
            QMessageBox.warning(
                self, DISPLAY_NAME,
                "That file doesn't contain a favorites list.",
            )
            return None
        paths = [p for p in raw if isinstance(p, str) and p.strip()]
        if not paths:
            QMessageBox.information(
                self, DISPLAY_NAME, "No favorites were found in that file."
            )
            return None
        # A favorites file exported on Windows carries drive-letter paths; map
        # them onto this machine so they resolve after import.
        if not sys.platform.startswith("win"):
            paths = [translate_windows_path(p) for p in paths]
        return paths

    # ---- Tree context menu + file operations ----------------------------- #
    def _tree_menu(self, pos: QPoint) -> None:
        item = self._tree.itemAt(pos)
        menu = QMenu(self)
        if item is not None and item.data(0, ROLE_KIND) in (
            "root", "dir", "file"
        ):
            path = item.data(0, ROLE_PATH)
            kind = item.data(0, ROLE_KIND)
            if kind == "file":
                menu.addAction("Open", lambda: self._open_file(path))
                fav = ("Remove from favorites" if self._is_favorite(path)
                       else "Add to favorites")
                menu.addAction(fav, lambda: self._toggle_favorite(path))
            new_menu = menu.addMenu("New file")
            new_menu.addAction(
                "Blank", lambda: self._new_in(path, kind, None)
            )
            for tname, tpath in list_templates():
                new_menu.addAction(
                    tname,
                    lambda checked=False, tp=tpath:
                    self._new_in(path, kind, tp),
                )
            new_menu.addSeparator()
            new_menu.addAction("Manage templates…",
                               self._open_templates_folder)
            menu.addAction("New folder…",
                           lambda: self._new_folder_in(path, kind))
            menu.addAction("Rename…", lambda: self._rename_path(path))
            menu.addAction("Delete", lambda: self._delete_path(path))
            menu.addSeparator()
            menu.addAction("Reveal in Explorer",
                           lambda: self._reveal(path))
            menu.addAction("Copy path",
                           lambda: QApplication.clipboard().setText(path))
        menu.addSeparator()
        menu.addAction(f"Import files into {INBOX_NAME}…",
                       self._import_to_inbox_dialog)
        menu.addAction("Manage folders…", self._manage_folders)
        menu.exec(self._tree.viewport().mapToGlobal(pos))

    @staticmethod
    def _dir_of(path: str, kind: str) -> str:
        return path if kind in ("root", "dir") else os.path.dirname(path)

    def _new_in(self, path: str, kind: str,
                template_path: str | None = None) -> None:
        folder = self._dir_of(path, kind)
        name, ok = QInputDialog.getText(self, "New file", "File name:",
                                        text="untitled.md")
        if not ok or not name.strip():
            return
        if os.path.splitext(name)[1] == "":
            name += ".md"
        target = os.path.join(folder, name)
        stem = os.path.splitext(os.path.basename(name))[0]
        content = self._template_content(template_path, stem)
        try:
            with open(target, "x", encoding="utf-8") as fh:
                fh.write(content)
        except OSError as exc:
            QMessageBox.warning(self, DISPLAY_NAME, f"Cannot create:\n{exc}")
            return
        self._reload_tree()
        self._open_file(target)

    def _new_folder_in(self, path: str, kind: str) -> None:
        folder = self._dir_of(path, kind)
        name, ok = QInputDialog.getText(self, "New folder", "Folder name:")
        if not ok or not name.strip():
            return
        try:
            os.makedirs(os.path.join(folder, name.strip()), exist_ok=False)
        except OSError as exc:
            QMessageBox.warning(self, DISPLAY_NAME, f"Cannot create:\n{exc}")
            return
        self._reload_tree()

    def _rename_path(self, path: str) -> None:
        name, ok = QInputDialog.getText(
            self, "Rename", "New name:", text=os.path.basename(path)
        )
        if not ok or not name.strip():
            return
        target = os.path.join(os.path.dirname(path), name.strip())
        try:
            os.rename(path, target)
        except OSError as exc:
            QMessageBox.warning(self, DISPLAY_NAME, f"Cannot rename:\n{exc}")
            return
        if self._current_path == path:
            self._current_path = target
            self._update_title()
        self._reload_tree()

    def _delete_path(self, path: str) -> None:
        answer = QMessageBox.question(
            self, DISPLAY_NAME,
            f"Send to the Recycle Bin?\n\n{path}",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            from send2trash import send2trash  # type: ignore[import-untyped]
            send2trash(path)
        except ImportError:
            # Send2Trash is in requirements.txt, so this should not happen --
            # but if it is missing from a build, deleting must not quietly
            # become permanent.  The dialog above says "delete", and a user
            # who has been told their files go to the Recycle Bin will not
            # look in it for something that never arrived.  Ask again, saying
            # plainly what is about to happen.
            confirmed = QMessageBox.question(
                self, DISPLAY_NAME,
                "This copy of MD Boss cannot use the Recycle Bin, so this "
                "would delete permanently:\n\n"
                f"{os.path.basename(path)}\n\n"
                "Delete it permanently?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No,
            )
            if confirmed != QMessageBox.StandardButton.Yes:
                return
            try:
                if os.path.isdir(path):
                    os.rmdir(path)
                else:
                    os.remove(path)
            except OSError as exc:
                QMessageBox.warning(self, DISPLAY_NAME,
                                    f"Cannot delete:\n{exc}")
                return
        except OSError as exc:
            QMessageBox.warning(self, DISPLAY_NAME, f"Cannot delete:\n{exc}")
            return
        if self._current_path == path:
            self._current_path = None
        self._reload_tree()

    def _reveal(self, path: str) -> None:
        try:
            if sys.platform.startswith("win"):
                subprocess.Popen(["explorer", "/select,", os.path.normpath(
                    path
                )])
            else:
                webbrowser.open(
                    QUrl.fromLocalFile(os.path.dirname(path)).toString()
                )
        except OSError:
            pass

    # ---- Pane toggles + folders ----------------------------------------- #
    def _toggle_editor(self) -> None:
        self._editor.setVisible(self._act_editor.isChecked())

    def _toggle_tree(self) -> None:
        self._left.setVisible(self._act_tree.isChecked())

    def _toggle_outline(self) -> None:
        self._mid.setVisible(self._act_outline.isChecked())

    def _toggle_yaml(self) -> None:
        update_config({"hide_front_matter": self._act_yaml.isChecked()})
        self._render_preview()

    # ---- Bidirectional editor <-> preview scroll sync ------------------- #
    def _sync_preview_scroll(self) -> None:
        """Scroll the preview to the editor's vertical position (fraction)."""
        if self._suppress_from_editor:
            return
        bar = self._editor.verticalScrollBar()
        span = bar.maximum() - bar.minimum()
        ratio = (bar.value() - bar.minimum()) / span if span > 0 else 0.0
        # Ignore the scroll echo the preview will report back for ~120 ms.
        self._suppress_from_preview = True
        # The JS is not an f-string: it is full of literal braces.
        script = (
            "(function(r){var h=document.documentElement;"
            "var max=h.scrollHeight-h.clientHeight;"
            "window.scrollTo(0, max>0?r*max:0);})(" + f"{ratio:.6f}" + ");"
        )
        self._preview.page().runJavaScript(script)
        QTimer.singleShot(120, self._clear_preview_suppress)

    def _clear_preview_suppress(self) -> None:
        self._suppress_from_preview = False

    def _on_preview_scrolled(self, ratio: float) -> None:
        """Scroll the editor to match a user scroll in the preview."""
        if self._suppress_from_preview:
            return
        bar = self._editor.verticalScrollBar()
        span = bar.maximum() - bar.minimum()
        if span <= 0:
            return
        clamped = min(1.0, max(0.0, ratio))
        # The resulting valueChanged echo is suppressed synchronously so it
        # does not bounce straight back to the preview.
        self._suppress_from_editor = True
        bar.setValue(bar.minimum() + round(clamped * span))
        self._suppress_from_editor = False

    def _manage_folders(self) -> None:
        dialog = ManageFoldersDialog(self._roots, self)
        if dialog.exec() == int(QDialog.DialogCode.Accepted):
            self._roots = self._sanitize_roots(dialog.roots())
            update_config({"roots": self._roots})
            self._reload_tree()
            self._render_preview()

    # ---- Help + updates -------------------------------------------------- #
    def _show_help(self) -> None:
        help_path = resource_path("HELP.md")
        try:
            with open(help_path, encoding="utf-8-sig") as fh:
                text = fh.read()
        except OSError:
            text = f"# {DISPLAY_NAME}\n\nVersion {APP_VERSION}."
        dialog = QDialog(self)
        dialog.setWindowTitle(f"{DISPLAY_NAME} Help")
        dialog.resize(760, 720)
        layout = QVBoxLayout(dialog)
        view = QWebEngineView(dialog)
        view.setPage(PreviewPage(view))
        base = QUrl.fromLocalFile(
            os.path.join(os.path.dirname(help_path), "")
        ).toString()
        view.setHtml(
            mdrender.render_document(
                text, base, title=f"{DISPLAY_NAME} Help",
            ),
            QUrl(base),
        )
        layout.addWidget(view, 1)
        footer = QHBoxLayout()
        footer.addWidget(QLabel(f"{DISPLAY_NAME} v{APP_VERSION}"))
        footer.addStretch(1)
        check = QPushButton("Check for updates", dialog)
        check.clicked.connect(lambda: self._start_update_check(manual=True))
        footer.addWidget(check)
        layout.addLayout(footer)
        dialog.exec()

    # ---- File associations ------------------------------------------------ #
    def _file_types_dialog(self) -> None:
        """Register or remove MD Boss as a handler for Markdown files."""
        command = handler_command()
        registered = is_registered(command)
        dialog = QDialog(self)
        dialog.setWindowTitle(f"{DISPLAY_NAME} — Markdown file types")
        dialog.resize(560, 260)
        layout = QVBoxLayout(dialog)
        exts = ", ".join(MARKDOWN_EXTS)
        state = ("MD Boss is registered as a Markdown handler for this user."
                 if registered else
                 "MD Boss is not currently registered as a Markdown handler.")
        blurb = QLabel(
            f"<p><b>{state}</b></p>"
            f"<p>Registering adds MD Boss to the <i>Open with</i> menu for "
            f"{exts}, and lists it under Settings &rarr; Default apps.</p>"
            "<p>Windows does not let an application make itself the default "
            "for a file type, so the last step is yours: right-click a "
            "Markdown file, choose <i>Open with &rarr; Choose another app</i>, "
            "pick MD Boss and tick <i>Always</i> — or set it in Default "
            "apps.</p>",
            dialog,
        )
        blurb.setWordWrap(True)
        layout.addWidget(blurb, 1)
        command_label = QLabel(f"<small><code>{command}</code></small>", dialog)
        command_label.setWordWrap(True)
        command_label.setToolTip("The command Windows will run")
        layout.addWidget(command_label)
        buttons = QDialogButtonBox(dialog)
        register = buttons.addButton(
            "Re-register" if registered else "Register",
            QDialogButtonBox.ButtonRole.ActionRole,
        )
        register.clicked.connect(lambda: self._set_registration(True, dialog))
        if registered:
            remove = buttons.addButton(
                "Remove", QDialogButtonBox.ButtonRole.DestructiveRole
            )
            remove.clicked.connect(
                lambda: self._set_registration(False, dialog)
            )
        settings = buttons.addButton(
            "Windows default apps…", QDialogButtonBox.ButtonRole.ActionRole
        )
        settings.clicked.connect(self._open_default_apps)
        buttons.addButton(QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)
        dialog.exec()

    def _set_registration(self, register: bool, dialog: QDialog) -> None:
        """Apply or undo the file-type registration, then report the result."""
        plan = current_registration_plan()
        try:
            if register:
                apply_registration(plan)
            else:
                remove_registration(plan)
        except OSError as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not update the registry:\n{exc}"
            )
            return
        notify_assoc_changed()
        dialog.accept()
        QMessageBox.information(
            self, DISPLAY_NAME,
            "MD Boss is now registered for Markdown files.\n\n"
            "To make it the default, right-click a .md file and use "
            "Open with → Choose another app → Always."
            if register else
            "MD Boss is no longer registered for Markdown files.",
        )

    def _open_default_apps(self) -> None:
        """Open the Windows Default apps settings page."""
        try:
            subprocess.Popen(["explorer", "ms-settings:defaultapps"])
        except OSError as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not open Windows settings:\n{exc}"
            )

    def _start_update_check(self, manual: bool = False) -> None:
        # The updater can self-install two ways: swap a Windows .exe, or
        # replace the running Linux AppImage.  A plain source/venv run on a
        # non-Windows box has nothing to self-replace, so there we just point
        # the user at the Releases page.
        if not sys.platform.startswith("win") and not running_appimage():
            if manual:
                box = QMessageBox(self)
                box.setWindowTitle(DISPLAY_NAME)
                box.setIcon(QMessageBox.Icon.Information)
                box.setText(
                    "This looks like a source or virtualenv run, which can't "
                    "self-update.\n\nUpdate by pulling the latest source "
                    "(git pull), or use the AppImage build, which updates "
                    "itself.  You can also download a release below."
                )
                open_btn = box.addButton("Open Releases page",
                                         QMessageBox.ButtonRole.AcceptRole)
                box.addButton(QMessageBox.StandardButton.Close)
                box.exec()
                if box.clickedButton() is open_btn:
                    webbrowser.open(RELEASES_URL)
            return
        self._update_manual = manual
        self._updater.start()

    def _on_update_ready(self, info: ReleaseInfo) -> None:
        current = parse_version(APP_VERSION) or (0,)
        if info["version"] <= current:
            if self._update_manual:
                QMessageBox.information(
                    self, DISPLAY_NAME,
                    f"You are up to date (v{APP_VERSION}).",
                )
            return
        if (not self._update_manual
                and load_config().get("skip_version") == info["version_str"]):
            return
        self._prompt_update(info)

    def _on_update_failed(self, message: str) -> None:
        if self._update_manual:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Update check failed:\n{message}"
            )

    def _prompt_update(self, info: ReleaseInfo) -> None:
        box = QMessageBox(self)
        box.setWindowTitle(DISPLAY_NAME)
        box.setIcon(QMessageBox.Icon.Question)
        box.setText(
            f"{DISPLAY_NAME} v{info['version_str']} is available "
            f"(you have v{APP_VERSION})."
        )
        box.setInformativeText(
            "Yes — download and install it now\n"
            "No — skip this version (won't ask again)\n"
            "Cancel — remind me next time"
        )
        box.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            | QMessageBox.StandardButton.Cancel
        )
        box.setDefaultButton(QMessageBox.StandardButton.Yes)
        choice = box.exec()
        if choice == QMessageBox.StandardButton.Cancel:
            return
        if choice == QMessageBox.StandardButton.No:
            update_config({"skip_version": info["version_str"]})
            return
        appimage = running_appimage()
        if appimage:
            url = info.get("appimage_url")
            if isinstance(url, str) and url:
                self._begin_appimage_download(info, url, appimage)
            else:
                webbrowser.open(info.get("html_url") or RELEASES_URL)
            return
        portable = running_portable()
        url = info["portable_url"] if portable else info["asset_url"]
        if getattr(sys, "frozen", False) and isinstance(url, str) and url:
            self._begin_download(info, url, portable)
        else:
            # Source checkout or no matching asset: don't install over a dev
            # tree -- just open the releases page.
            webbrowser.open(info.get("html_url") or RELEASES_URL)

    def _begin_download(
        self, info: ReleaseInfo, url: str, portable: bool
    ) -> None:
        self._dl_portable = portable
        stem = ("MDBoss-Portable-%s.zip" if portable
                else "MDBoss-Setup-%s.exe") % info["version_str"]
        dest = os.path.join(tempfile.gettempdir(), stem)
        dialog = QProgressDialog(
            f"Downloading update… {DISPLAY_NAME} will restart when ready.",
            "", 0, 0, self,
        )
        dialog.setWindowTitle(f"Updating {DISPLAY_NAME}")
        dialog.setCancelButton(None)
        dialog.setWindowModality(Qt.WindowModality.WindowModal)
        dialog.setMinimumDuration(0)
        dialog.show()
        self._dl_dialog = dialog
        self._downloader = Downloader(url, dest, self)
        self._downloader.done.connect(self._on_download_done)
        self._downloader.failed.connect(self._on_download_failed)
        self._downloader.start()

    def _begin_appimage_download(
        self, info: ReleaseInfo, url: str, current_path: str
    ) -> None:
        """Download the new AppImage beside the running one, then swap it in.

        The download lands in the target's own directory so the final swap is
        an atomic same-filesystem rename onto a fresh inode -- the running
        (mounted) AppImage keeps its old inode until this process exits."""
        self._appimage_target = current_path
        dest = os.path.join(
            os.path.dirname(current_path),
            f".MDBoss-update-{info['version_str']}.AppImage",
        )
        dialog = QProgressDialog(
            f"Downloading update… {DISPLAY_NAME} will restart when ready.",
            "", 0, 0, self,
        )
        dialog.setWindowTitle(f"Updating {DISPLAY_NAME}")
        dialog.setCancelButton(None)
        dialog.setWindowModality(Qt.WindowModality.WindowModal)
        dialog.setMinimumDuration(0)
        dialog.show()
        self._dl_dialog = dialog
        self._downloader = Downloader(url, dest, self)
        self._downloader.done.connect(self._on_appimage_download_done)
        self._downloader.failed.connect(self._on_download_failed)
        self._downloader.start()

    def _on_appimage_download_done(self, dest: str) -> None:
        if self._dl_dialog is not None:
            self._dl_dialog.close()
        self._swap_appimage_and_exit(dest)

    def _swap_appimage_and_exit(self, new_path: str) -> None:
        target = self._appimage_target
        if not target:
            return
        if not self._confirm_discard():
            try:
                os.remove(new_path)
            except OSError:
                pass
            return
        try:
            os.chmod(new_path, 0o755)
            os.replace(new_path, target)         # atomic, same filesystem
        except OSError as exc:
            try:
                os.remove(new_path)
            except OSError:
                pass
            QMessageBox.warning(
                self, DISPLAY_NAME,
                f"Could not install the update:\n{exc}\n\nYou can download it "
                "manually from the releases page on GitHub.",
            )
            return
        # Relaunch the freshly-installed AppImage once this process exits.
        subprocess.Popen([target], start_new_session=True)
        self._updating = True
        self.close()

    def _on_download_done(self, dest: str) -> None:
        if self._dl_dialog is not None:
            self._dl_dialog.close()
        if self._dl_portable:
            self._swap_portable_and_exit(dest)
        else:
            self._launch_installer_and_exit(dest)

    def _on_download_failed(self, message: str) -> None:
        if self._dl_dialog is not None:
            self._dl_dialog.close()
        QMessageBox.warning(
            self, DISPLAY_NAME,
            f"The download failed:\n{message}\n\nYou can download the update "
            "manually from the releases page on GitHub.",
        )

    def _launch_installer_and_exit(self, setup_path: str) -> None:
        if not self._confirm_discard():
            return
        batch_path = setup_path + ".cmd"
        try:
            with open(batch_path, "w", encoding="ascii",
                      errors="replace") as fh:
                fh.write(_installer_batch(setup_path, sys.executable))
        except OSError as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not start the update:\n{exc}"
            )
            return
        self._spawn_handoff_batch(batch_path)
        self._updating = True
        self.close()

    def _swap_portable_and_exit(self, zip_path: str) -> None:
        staging = zip_path + ".new"
        try:
            source = extract_portable(zip_path, staging)
        except (OSError, ValueError, zipfile.BadZipFile) as exc:
            shutil.rmtree(staging, ignore_errors=True)
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not unpack the update:\n{exc}"
            )
            return
        if not self._confirm_discard():
            shutil.rmtree(staging, ignore_errors=True)
            return
        batch_path = zip_path + ".cmd"
        try:
            with open(batch_path, "w", encoding="ascii",
                      errors="replace") as fh:
                fh.write(_portable_batch(source, sys.executable, [zip_path],
                                         staging))
        except OSError as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not start the update:\n{exc}"
            )
            return
        self._spawn_handoff_batch(batch_path)
        self._updating = True
        self.close()

    def _spawn_handoff_batch(self, batch_path: str) -> None:
        """Run the update-handoff batch hidden and detached.

        PyInstaller's ``_PYI_*`` / ``_MEIPASS2`` env vars must be stripped, or
        the relaunched exe reuses this process's extraction dir (deleted on
        exit) and fails to start.
        """
        env = {k: v for k, v in os.environ.items()
               if not k.startswith("_PYI_") and k != "_MEIPASS2"}
        flags = (getattr(subprocess, "CREATE_NO_WINDOW", 0)
                 | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
        try:
            subprocess.Popen(
                ["cmd", "/c", batch_path], creationflags=flags,
                close_fds=True, env=env, cwd=os.path.dirname(batch_path),
            )
        except OSError:
            pass

    # ---- Single instance -------------------------------------------------- #
    def _start_ipc_server(self) -> None:
        """Listen for later launches so one window serves every document."""
        self._ipc = QLocalServer(self)
        self._ipc.newConnection.connect(self._on_ipc_connection)
        if not self._ipc.listen(IPC_SERVER_NAME):
            # A crashed instance can leave the name behind; take it over.
            QLocalServer.removeServer(IPC_SERVER_NAME)
            self._ipc.listen(IPC_SERVER_NAME)

    def _on_ipc_connection(self) -> None:
        """Open the document another launch handed us, and come to the front."""
        conn = self._ipc.nextPendingConnection()
        if conn is None:
            return
        path = ""
        if conn.waitForReadyRead(IPC_TIMEOUT_MS):
            path = bytes(conn.readAll().data()).decode("utf-8", "replace")
        conn.disconnectFromServer()
        self._raise_to_front()
        if path:
            self.open_path(path)

    def _raise_to_front(self) -> None:
        if self.isMinimized():
            self.showNormal()
        self.raise_()
        self.activateWindow()

    # ---- Geometry persistence ------------------------------------------- #
    def _restore_geometry(self, cfg: ConfigDict) -> None:
        geo = cfg.get("geometry")
        if isinstance(geo, str):
            self.restoreGeometry(QByteArray.fromBase64(geo.encode("ascii")))
        # split_left_v2: the left splitter gained a third pane (Recent), and a
        # saved two-pane state would be applied to the wrong widgets.  The new
        # key makes pre-Recent configs fall back to the default sizes.
        for key, split in (("split_main", self._main_split),
                           ("split_left_v2", self._left),
                           ("split_right", self._right)):
            state = cfg.get(key)
            if isinstance(state, str):
                split.restoreState(
                    QByteArray.fromBase64(state.encode("ascii"))
                )

    def closeEvent(  # noqa: N802 (Qt override)
        self, event: QCloseEvent
    ) -> None:
        # During an update the discard prompt already ran; don't ask twice.
        if not self._updating and not self._confirm_discard():
            event.ignore()
            return
        update_config({
            "roots": self._roots,
            "favorites": self._favorites,
            "recents": self._recents,
            "geometry": _b64(self.saveGeometry()),
            "split_main": _b64(self._main_split.saveState()),
            "split_left_v2": _b64(self._left.saveState()),
            "split_right": _b64(self._right.saveState()),
            "show_files": self._act_tree.isChecked(),
            "show_outline": self._act_outline.isChecked(),
            "show_editor": self._act_editor.isChecked(),
        })
        event.accept()


def cli_path(argv: list[str]) -> str | None:
    """The document path in ``argv``, or None.

    Windows hands the associated file to the shell handler as a single
    argument, quoted, so only the first matters.  A leading ``-`` marks a Qt
    switch (``-platform`` and friends), never a document.
    """
    assert isinstance(argv, list), "argv must be a list"
    if len(argv) < 2:
        return None
    first = argv[1]
    return first if first and not first.startswith("-") else None


def _grant_foreground_to_receiver() -> None:
    """Let the instance we are forwarding to take the foreground.

    Windows only lets a process take the foreground if it holds foreground
    rights -- which the long-running instance does not, so its
    ``activateWindow()`` alone is quietly reduced to a taskbar flash and the
    double-clicked document opens *behind* whatever the user was doing.  THIS
    process was just launched by that double-click, so it does hold the
    rights; hand them over before forwarding the path.  ASFW_ANY because the
    local socket does not expose the server's pid, and the grant lapses at
    the next user input anyway.
    """
    if not sys.platform.startswith("win"):
        return
    asfw_any = ctypes.c_ulong(0xFFFFFFFF)          # ((DWORD)-1): any process
    try:
        granted = bool(
            ctypes.windll.user32.AllowSetForegroundWindow(asfw_any)
        )
    except (AttributeError, OSError):
        granted = False
    if not granted:
        # We had no rights to give (e.g. launched from a background script).
        # The receiver's raise then degrades to the old taskbar flash.
        return


def forward_to_running(path: str | None) -> bool:
    """Hand ``path`` to an already-running MD Boss.  True when one took it.

    Keeps a single window when the app is launched per double-clicked file.
    Falling through to False -- no instance, or one too busy to answer within
    the timeout -- simply starts a normal window.
    """
    socket = QLocalSocket()
    socket.connectToServer(IPC_SERVER_NAME)
    if not socket.waitForConnected(IPC_TIMEOUT_MS):
        return False
    _grant_foreground_to_receiver()
    socket.write((path or "").encode("utf-8"))
    socket.flush()
    delivered = socket.waitForBytesWritten(IPC_TIMEOUT_MS)
    socket.disconnectFromServer()
    return bool(delivered)


def handle_registration_flags(argv: list[str]) -> int | None:
    """Run a ``--(un)register-file-types`` request, if ``argv`` holds one.

    The installer calls these so there is one implementation of the registry
    layout rather than a second copy in the .iss.  Returns a process exit code
    when a flag was handled -- no window is created -- or None to carry on and
    start normally.  Off Windows the flags are accepted and do nothing: Linux
    associations come from the .desktop entry install-linux.sh writes.
    """
    assert isinstance(argv, list), "argv must be a list"
    register = REGISTER_FLAG in argv
    if not register and UNREGISTER_FLAG not in argv:
        return None
    if not sys.platform.startswith("win"):
        return 0
    try:
        plan = current_registration_plan()
        if register:
            apply_registration(plan)
        else:
            remove_registration(plan)
    except OSError:
        return 1
    notify_assoc_changed()
    return 0


def main() -> None:
    """Create the application and show the main window."""
    code = handle_registration_flags(sys.argv)
    if code is not None:
        sys.exit(code)
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    path = cli_path(sys.argv)
    if forward_to_running(path):
        return                          # an existing window took it; done
    seed_templates()
    window = MainWindow()
    window.show()
    if path:
        window.open_path(path)
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
