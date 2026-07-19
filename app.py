"""MDBoss -- a local Markdown manager, editor, and GitHub-style viewer.

MDBoss browses Markdown files across up to five root folders, edits them in a
source pane, and renders a live preview using the GitHub-light theme with
mermaid diagrams and embedded images.  It is a PySide6 sibling of PDF Sherpa
and reuses that app's conventions: a single-file GUI monolith over a small pure
helper module (``mdrender``), a read-merge-write JSON config under
``%APPDATA%``, and the same PyInstaller -> Inno -> portable-zip release
pipeline with an in-app GitHub self-updater.

Rendering is 100% offline (bundled mermaid + CSS + Pygments) and the preview's
web view is network-locked: every request whose scheme is not local is blocked,
so a stray remote image or link in a document can never reach the network.
"""

from __future__ import annotations

import datetime
import json
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

from PySide6.QtCore import (
    QByteArray, QEvent, QObject, QPoint, QRect, QSize, Qt, QTimer, QUrl, Signal,
    Slot,
)
from PySide6.QtGui import (
    QAction, QCloseEvent, QColor, QDragEnterEvent, QDragMoveEvent, QDropEvent,
    QFont, QIcon, QKeySequence, QPaintEvent, QPainter, QResizeEvent,
    QTextCharFormat, QTextFormat,
)
from PySide6.QtWebEngineCore import (
    QWebEnginePage, QWebEngineProfile, QWebEngineSettings,
    QWebEngineUrlRequestInfo, QWebEngineUrlRequestInterceptor,
)
from PySide6.QtWebChannel import QWebChannel
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import (
    QApplication, QDialog, QDialogButtonBox, QFileDialog,
    QHBoxLayout, QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem,
    QMainWindow, QMenu, QMessageBox, QPlainTextEdit, QProgressDialog,
    QPushButton, QSplitter, QTextEdit, QToolBar, QToolButton, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
)

import mdrender

APP_VERSION = "0.1.11"
APP_NAME = "MDBoss"            # config folder, exe name, process name
DISPLAY_NAME = "MD Boss"       # human-facing name (installer, window title)

# In-app updater: asset names are load-bearing -- release.ps1 publishes exactly
# these and the updater matches them by name.
UPDATE_API_URL = (
    "https://api.github.com/repos/Flinterpop/MDBoss/releases/latest"
)
UPDATE_ASSET_NAME = "MDBoss-Setup.exe"
UPDATE_PORTABLE_ASSET_NAME = "MDBoss-Portable.zip"
RELEASES_URL = "https://github.com/Flinterpop/MDBoss/releases/latest"

MAX_ROOTS = 5
MAX_FAVORITES = 10
MARKDOWN_EXTS = (".md", ".markdown", ".mdown", ".mkd", ".mdwn")
# Drag-and-drop ingest: dropped Markdown files are copied here.  Drops are only
# accepted when a folder of this name exists at the top level of a root folder
# (or a root is itself named this), giving them a defined home.
INBOX_NAME = "MD_Inbox"
# Drag/drop event types routed through the app-level filter so a drop lands in
# MD_Inbox no matter which widget is under the cursor (the preview's native web
# widget would otherwise swallow it).
_DND_EVENT_TYPES = frozenset({
    QEvent.Type.DragEnter, QEvent.Type.DragMove, QEvent.Type.Drop,
})
PREVIEW_DEBOUNCE_MS = 300

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
        with open(config_path(), "r", encoding="utf-8") as fh:
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
    for entry in data.get("assets", []):
        name = str(entry.get("name", ""))
        url = entry.get("browser_download_url")
        if name == UPDATE_ASSET_NAME:
            asset = url
        elif name == UPDATE_PORTABLE_ASSET_NAME:
            portable = url
    return {
        "version": version,
        "version_str": str(data.get("tag_name", "")).lstrip("vV"),
        "asset_url": asset,
        "portable_url": portable,
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
    while os.path.exists(candidate) and counter < 10000:   # bounded (Rule of 10)
        candidate = os.path.join(dest_dir, f"{stem} ({counter}){ext}")
        counter += 1
    return candidate


# Header for update-handoff batches: wait until every MDBoss.exe process has
# exited (up to ~60s) before touching the on-disk exe.  A fixed delay is not
# enough -- the large one-file bundle can take several seconds to unpack-clean
# and release the exe lock, which would make the installer fail and the old
# version relaunch (an update loop).
_WAIT_FOR_EXIT = (
    "@echo off\r\n"
    "timeout /t 2 /nobreak >nul\r\n"
    "set /a _n=0\r\n"
    ":mdwait\r\n"
    'tasklist /FI "IMAGENAME eq MDBoss.exe" 2>nul | '
    'find /I "MDBoss.exe" >nul\r\n'
    "if errorlevel 1 goto mdgo\r\n"
    "set /a _n+=1\r\n"
    "if %_n% GEQ 60 goto mdgo\r\n"
    "timeout /t 1 /nobreak >nul\r\n"
    "goto mdwait\r\n"
    ":mdgo\r\n"
)


def _installer_batch(setup_path: str, app_exe: str) -> str:
    """Batch that installs an update and relaunches, run after we exit.

    Waits for every MDBoss.exe to exit (so the exe lock is released), installs
    silently, relaunches, then cleans up.  Each line runs even if an earlier
    one failed, so a failed install still relaunches the intact old exe.
    """
    return (
        _WAIT_FOR_EXIT
        + f'"{setup_path}" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES\r\n'
        + f'start "" "{app_exe}"\r\n'
        + f'del /q "{setup_path}"\r\n'
        + 'del /q "%~f0"\r\n'
    )


def _portable_batch(zip_exe: str, app_exe: str, cleanup: list[str]) -> str:
    """Batch that swaps a portable exe with a freshly unpacked one."""
    lines = [
        _WAIT_FOR_EXIT,
        f'move /y "{zip_exe}" "{app_exe}"\r\n',
        f'start "" "{app_exe}"\r\n',
    ]
    lines += [f'del /q "{path}"\r\n' for path in cleanup]
    lines.append('del /q "%~f0"\r\n')
    return "".join(lines)


def _norm(path: str) -> str:
    """Case/format-normalised absolute path, for use as a dict key."""
    return os.path.normcase(os.path.abspath(path))


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

    def __init__(self, editor: "CodeEditor") -> None:
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

    # External file drops are for the window's MD_Inbox ingest, not for
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
        fav_raw = cfg.get("favorites", [])
        self._favorites: list[str] = (
            [p for p in fav_raw if isinstance(p, str)]
            if isinstance(fav_raw, list) else []
        )[:MAX_FAVORITES]
        self._current_path: str | None = None   # open document, or None
        self._suppress_dirty = False             # True while loading a file
        self._watched_roots: set[str] = set()
        # Path of the MD_Inbox drop folder, or None.  Recomputed on every tree
        # reload; gates whether dragged-in Markdown files are accepted.
        self._inbox_path: str | None = None
        # Recursive Markdown counts per folder, shown beside folder names.
        self._md_counts: dict[str, int] = {}
        # Hide a leading YAML front-matter block in the preview (default on).
        self._init_hide_yaml = bool(cfg.get("hide_front_matter", True))
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
        self._updating = False                   # skip close-time re-ask

        self.setWindowTitle(f"{DISPLAY_NAME} - v{APP_VERSION}")
        self.setAcceptDrops(True)                # MD_Inbox drag-and-drop ingest
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
        self._restore_geometry(cfg)
        self._reload_tree()
        self._reload_favorites()
        self._render_preview()

        # The in-app updater installs a Windows .exe, so it only runs there.
        if cfg.get("check_updates", True) and sys.platform.startswith("win"):
            QTimer.singleShot(2000, self._start_update_check)

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
        add("New", self._new_file, "Ctrl+N", "Create a new Markdown file")
        add("New from template…", self._new_from_template_dialog,
            tip="Create a new file from a template")
        add("Save", self._save_file, "Ctrl+S", "Save the current document")
        bar.addSeparator()
        # Toggles ordered to match the columns: Files | Outline | Edit.
        self._act_tree = add("Files", self._toggle_tree,
                             tip="Show or hide the file tree")
        self._act_tree.setCheckable(True)
        self._act_tree.setChecked(True)
        self._act_outline = add("Outline", self._toggle_outline,
                                tip="Show or hide the outline pane")
        self._act_outline.setCheckable(True)
        self._act_outline.setChecked(True)
        self._act_editor = add("Edit", self._toggle_editor,
                               tip="Show or hide the source editor")
        self._act_editor.setCheckable(True)
        self._act_editor.setChecked(True)
        self._act_yaml = add(
            "Hide YAML", self._toggle_yaml,
            tip="Hide a YAML front-matter block at the top of the file",
        )
        self._act_yaml.setCheckable(True)
        self._act_yaml.setChecked(self._init_hide_yaml)
        bar.addSeparator()
        add("Help", self._show_help, "F1", "About MDBoss")

    def _build_panes(self) -> None:
        # Pane 1: Favorites over the file list, in a vertical splitter so the
        # Favorites area can be dragged taller or shorter.
        left = QSplitter(Qt.Orientation.Vertical, self)

        fav_box = QWidget(self)
        fav_layout = QVBoxLayout(fav_box)
        fav_layout.setContentsMargins(4, 4, 4, 2)
        fav_header = QHBoxLayout()
        fav_header.addWidget(QLabel("Favorites"))
        fav_header.addStretch(1)
        fav_manage = QToolButton(self)
        fav_manage.setText("⋯")
        fav_manage.setToolTip("Clear, export, or import your favorites")
        fav_manage.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        fav_menu = QMenu(fav_manage)
        fav_menu.addAction("Export favorites…", self._export_favorites)
        fav_menu.addAction("Import favorites…", self._import_favorites)
        fav_menu.addSeparator()
        fav_menu.addAction("Clear all favorites", self._clear_favorites)
        fav_manage.setMenu(fav_menu)
        fav_header.addWidget(fav_manage)
        fav_layout.addLayout(fav_header)
        self._fav_list = QListWidget(self)
        self._fav_list.itemActivated.connect(self._on_favorite_activated)
        self._fav_list.itemClicked.connect(self._on_favorite_activated)
        self._fav_list.setContextMenuPolicy(
            Qt.ContextMenuPolicy.CustomContextMenu
        )
        self._fav_list.customContextMenuRequested.connect(self._fav_menu)
        fav_layout.addWidget(self._fav_list, 1)
        left.addWidget(fav_box)

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
        left.addWidget(files_box)

        fav_box.setMinimumHeight(56)
        files_box.setMinimumHeight(120)
        left.setChildrenCollapsible(False)
        left.setStretchFactor(1, 1)          # the file list takes extra space
        left.setSizes([150, 520])

        # Pane 2: document outline.
        outline_box = QWidget(self)
        ob_layout = QVBoxLayout(outline_box)
        ob_layout.setContentsMargins(4, 4, 4, 4)
        ob_layout.addWidget(QLabel("Outline"))
        self._outline = QListWidget(self)
        self._outline.itemActivated.connect(self._on_outline_activated)
        self._outline.itemClicked.connect(self._on_outline_activated)
        ob_layout.addWidget(self._outline, 1)
        self._mid = outline_box

        # Pane 3: editor | preview.
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
        self._preview.setAcceptDrops(False)      # drops belong to MD_Inbox
        self._preview.setPage(PreviewPage(self._preview))
        # On non-Windows an app-wide event filter crashes (see __init__), so
        # watch just the preview and its lazily-created native child so a
        # Markdown file dropped over the preview still reaches MD_Inbox.
        if not sys.platform.startswith("win"):
            self._preview.installEventFilter(self)

            def _watch_preview_child() -> None:
                child = self._preview.focusProxy()
                if child is not None:
                    child.setAcceptDrops(True)
                    child.installEventFilter(self)

            QTimer.singleShot(0, _watch_preview_child)
            self._preview.loadFinished.connect(
                lambda _ok: _watch_preview_child()
            )
        # Web channel: the page reports its own scroll position back to Qt for
        # preview -> editor sync (editor -> preview is driven from Qt).
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
        self._right = QSplitter(Qt.Orientation.Horizontal, self)
        self._right.addWidget(self._editor)
        self._right.addWidget(self._preview)
        self._right.setSizes([500, 700])

        # Minimum widths so no column can be dragged or restored to zero.
        left.setMinimumWidth(160)
        outline_box.setMinimumWidth(150)
        self._right.setChildrenCollapsible(False)
        self._main_split = QSplitter(Qt.Orientation.Horizontal, self)
        self._main_split.addWidget(left)
        self._main_split.addWidget(outline_box)
        self._main_split.addWidget(self._right)
        self._main_split.setStretchFactor(2, 1)
        self._main_split.setChildrenCollapsible(False)
        self._main_split.setSizes([300, 200, 780])
        self._left = left
        self.setCentralWidget(self._main_split)

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
                sub = self._md_counts.get(_norm(entry.path), 0)
                child = QTreeWidgetItem(item, [f"{entry.name}  ({sub})"])
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

    # ---- Drag-and-drop ingest into MD_Inbox ------------------------------ #
    def _accepts_drop(self, event: QDropEvent) -> bool:
        """True when the drag carries at least one Markdown file and an
        MD_Inbox folder currently exists to receive it."""
        inbox = self._inbox_path
        if not inbox or not os.path.isdir(inbox):
            return False
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
        # App-wide catch for Markdown-file drags so they reach MD_Inbox even
        # over the preview's native web widget.  Non-file drags fall through to
        # normal handling (e.g. moving text within the editor).
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
        """Accept ``event`` and ingest the Markdown files it carries."""
        event.acceptProposedAction()
        files = [
            url.toLocalFile() for url in event.mimeData().urls()
            if url.isLocalFile() and is_markdown(url.toLocalFile())
        ]
        self._ingest_dropped(files)

    def _ingest_dropped(self, files: list[str]) -> None:
        """Copy dropped Markdown ``files`` into MD_Inbox, refresh the tree, and
        open the first.  A file already inside the inbox is opened in place."""
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
    def _open_file(self, path: str) -> None:
        if not path or not os.path.isfile(path):
            return
        if not self._confirm_discard():
            return
        try:
            with open(path, "r", encoding="utf-8") as fh:
                text = fh.read()
        except (OSError, ValueError) as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Cannot open file:\n{exc}"
            )
            return
        self._suppress_dirty = True
        self._editor.setPlainText(text)
        self._editor.document().setModified(False)
        self._suppress_dirty = False
        self._current_path = path
        self._render_preview()
        self._update_title()

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
            with open(template_path, "r", encoding="utf-8") as fh:
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
            "var el=document.getElementById(%s);"
            "if(el){el.scrollIntoView({behavior:'smooth',block:'start'});}"
            % json.dumps(str(slug))
        )
        self._preview.page().runJavaScript(script)

    # ---- Favorites ------------------------------------------------------- #
    # Favorites are pinned documents, newest first, capped at MAX_FAVORITES;
    # adding an 11th drops the oldest.  Stored as absolute paths and persisted.
    def _is_favorite(self, path: str) -> bool:
        want = _norm(path)
        return any(_norm(f) == want for f in self._favorites)

    def _reload_favorites(self) -> None:
        self._fav_list.clear()
        if not self._favorites:
            hint = QListWidgetItem("(right-click a file to add)")
            hint.setForeground(QColor("#999"))
            hint.setFlags(Qt.ItemFlag.NoItemFlags)
            self._fav_list.addItem(hint)
            return
        for path in self._favorites:
            item = QListWidgetItem(os.path.basename(path))  # filename only
            item.setToolTip(path)                           # full path (hover)
            item.setData(int(Qt.ItemDataRole.UserRole), path)
            if not os.path.isfile(path):
                item.setForeground(QColor("#c01c28"))        # missing file
            self._fav_list.addItem(item)

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
            with open(path, "r", encoding="utf-8") as fh:
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
        script = (
            "(function(r){var h=document.documentElement;"
            "var max=h.scrollHeight-h.clientHeight;"
            "window.scrollTo(0, max>0?r*max:0);})(%.6f);" % ratio
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
            with open(help_path, "r", encoding="utf-8") as fh:
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

    def _start_update_check(self, manual: bool = False) -> None:
        # The bundled updater downloads and swaps a Windows .exe; on other
        # platforms send the user to the Releases page instead.
        if not sys.platform.startswith("win"):
            if manual:
                box = QMessageBox(self)
                box.setWindowTitle(DISPLAY_NAME)
                box.setIcon(QMessageBox.Icon.Information)
                box.setText(
                    "Automatic updates are Windows-only.\n\n"
                    "On Linux, update by pulling the latest source "
                    "(git pull) or download a release from the project page."
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
        new_exe = zip_path + ".new.exe"
        try:
            with zipfile.ZipFile(zip_path) as archive:
                member = next(
                    (n for n in archive.namelist()
                     if n.lower().endswith(".exe")), None
                )
                if member is None:
                    raise ValueError("no exe inside the update zip")
                with archive.open(member) as src, open(new_exe, "wb") as out:
                    shutil.copyfileobj(src, out)
        except (OSError, ValueError, zipfile.BadZipFile) as exc:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Could not unpack the update:\n{exc}"
            )
            return
        if not self._confirm_discard():
            return
        batch_path = zip_path + ".cmd"
        try:
            with open(batch_path, "w", encoding="ascii",
                      errors="replace") as fh:
                fh.write(_portable_batch(new_exe, sys.executable, [zip_path]))
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

    # ---- Geometry persistence ------------------------------------------- #
    def _restore_geometry(self, cfg: ConfigDict) -> None:
        geo = cfg.get("geometry")
        if isinstance(geo, str):
            self.restoreGeometry(QByteArray.fromBase64(geo.encode("ascii")))
        for key, split in (("split_main", self._main_split),
                           ("split_left", self._left),
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
            "geometry": _b64(self.saveGeometry()),
            "split_main": _b64(self._main_split.saveState()),
            "split_left": _b64(self._left.saveState()),
            "split_right": _b64(self._right.saveState()),
        })
        event.accept()


def main() -> None:
    """Create the application and show the main window."""
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    seed_templates()
    window = MainWindow()
    window.show()
    # Open a file passed on the command line, if any.
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        window._open_file(os.path.abspath(sys.argv[1]))  # noqa: SLF001
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
