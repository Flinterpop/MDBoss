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

import json
import os
import subprocess
import sys
import threading
import urllib.request
import webbrowser
from collections.abc import Callable
from typing import TypedDict

from PySide6.QtCore import (
    QByteArray, QObject, QPoint, QRect, QSize, Qt, QTimer, QUrl, Signal,
)
from PySide6.QtGui import (
    QAction, QCloseEvent, QColor, QFont, QIcon, QKeySequence, QPaintEvent,
    QPainter, QResizeEvent, QTextCharFormat, QTextFormat,
)
from PySide6.QtWebEngineCore import (
    QWebEnginePage, QWebEngineProfile, QWebEngineSettings,
    QWebEngineUrlRequestInfo, QWebEngineUrlRequestInterceptor,
)
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import (
    QApplication, QDialog, QDialogButtonBox, QFileDialog,
    QHBoxLayout, QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem,
    QMainWindow, QMenu, QMessageBox, QPlainTextEdit, QPushButton, QSplitter,
    QTextEdit, QToolBar, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)

import mdrender

APP_VERSION = "0.1.0"
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
def config_path() -> str:
    """Per-user settings file, stable across source and frozen runs."""
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
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


def asset_url(name: str) -> str:
    """A ``file:///`` URL to a bundled ``assets/`` file, for the web view."""
    path = resource_path(os.path.join("assets", name))
    return QUrl.fromLocalFile(path).toString()


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


def _b64(data: QByteArray) -> str:
    """Base64-encode a QByteArray to an ASCII str for JSON config."""
    return bytes(data.toBase64().data()).decode("ascii")


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

        # --- Web view assets + network lock. ---
        self._gh_css = asset_url("github-markdown-light.css")
        self._pyg_css = asset_url("pygments-github.css")
        self._mermaid_js = asset_url("mermaid.min.js")
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

        self.setWindowTitle(f"{DISPLAY_NAME} - v{APP_VERSION}")
        self.resize(1280, 800)
        icon = resource_path("mdbossicon.ico")
        if os.path.isfile(icon):
            self.setWindowIcon(QIcon(icon))

        self._build_toolbar()
        self._build_panes()
        self._restore_geometry(cfg)
        self._reload_tree()
        self._reload_favorites()
        self._render_preview()

        if cfg.get("check_updates", True):
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
        add("Save", self._save_file, "Ctrl+S", "Save the current document")
        bar.addSeparator()
        self._act_editor = add("Edit", self._toggle_editor,
                               tip="Show or hide the source editor")
        self._act_editor.setCheckable(True)
        self._act_editor.setChecked(True)
        self._act_tree = add("Files", self._toggle_tree,
                             tip="Show or hide the file tree")
        self._act_tree.setCheckable(True)
        self._act_tree.setChecked(True)
        self._act_outline = add("Outline", self._toggle_outline,
                                tip="Show or hide the outline pane")
        self._act_outline.setCheckable(True)
        self._act_outline.setChecked(True)
        bar.addSeparator()
        add("Help", self._show_help, "F1", "About MDBoss")

    def _build_panes(self) -> None:
        # Pane 1: filter box + multi-root file tree.
        left = QWidget(self)
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(4, 4, 4, 4)
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
        left_layout.addLayout(filter_row)

        self._tree = QTreeWidget(self)
        self._tree.setHeaderHidden(True)
        self._tree.setContextMenuPolicy(
            Qt.ContextMenuPolicy.CustomContextMenu
        )
        self._tree.customContextMenuRequested.connect(self._tree_menu)
        self._tree.itemExpanded.connect(self._on_item_expanded)
        self._tree.itemActivated.connect(self._on_item_activated)
        self._tree.itemClicked.connect(self._on_item_activated)
        left_layout.addWidget(self._tree, 1)

        # Pane 2: outline over favorites.
        mid = QSplitter(Qt.Orientation.Vertical, self)
        outline_box = QWidget(self)
        ob_layout = QVBoxLayout(outline_box)
        ob_layout.setContentsMargins(4, 4, 4, 4)
        ob_layout.addWidget(QLabel("Outline"))
        self._outline = QListWidget(self)
        self._outline.itemActivated.connect(self._on_outline_activated)
        self._outline.itemClicked.connect(self._on_outline_activated)
        ob_layout.addWidget(self._outline, 1)
        mid.addWidget(outline_box)

        fav_box = QWidget(self)
        fb_layout = QVBoxLayout(fav_box)
        fb_layout.setContentsMargins(4, 4, 4, 4)
        fb_layout.addWidget(QLabel("Favorites"))
        self._fav_list = QListWidget(self)
        self._fav_list.itemActivated.connect(self._on_favorite_activated)
        self._fav_list.itemClicked.connect(self._on_favorite_activated)
        fb_layout.addWidget(self._fav_list, 1)
        mid.addWidget(fav_box)
        self._mid = mid

        # Pane 3: editor | preview.
        self._editor = CodeEditor(self)
        self._editor.setPlaceholderText(
            "Select a Markdown file, or press Ctrl+N to create one."
        )
        self._editor.textChanged.connect(self._on_text_changed)
        self._editor.document().modificationChanged.connect(
            self._on_modified_changed
        )
        self._preview = QWebEngineView(self)
        self._preview.setPage(PreviewPage(self._preview))
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

        self._main_split = QSplitter(Qt.Orientation.Horizontal, self)
        self._main_split.addWidget(left)
        self._main_split.addWidget(mid)
        self._main_split.addWidget(self._right)
        self._main_split.setStretchFactor(2, 1)
        self._main_split.setSizes([260, 220, 800])
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
        for root in self._roots:
            item = QTreeWidgetItem(self._tree, [root["name"]])
            item.setData(0, ROLE_PATH, root["path"])
            item.setData(0, ROLE_KIND, "root")
            item.setForeground(0, QColor("#0a58ca"))
            self._add_placeholder(item)
        self._refresh_watcher()
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
                child = QTreeWidgetItem(item, [entry.name])
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
        if not self._confirm_discard():
            return
        self._suppress_dirty = True
        self._editor.setPlainText("# New document\n\n")
        self._editor.document().setModified(True)
        self._suppress_dirty = False
        self._current_path = None
        self._render_preview()
        self._update_title()

    def _update_title(self) -> None:
        name = (os.path.basename(self._current_path)
                if self._current_path else "Untitled")
        dirty = "*" if self._editor.document().isModified() else ""
        self.setWindowTitle(f"{dirty}{name} - {DISPLAY_NAME} v{APP_VERSION}")

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
        html = mdrender.render_document(
            text, base, self._gh_css, self._pyg_css, self._mermaid_js,
            title=os.path.basename(self._current_path or "MDBoss"),
        )
        self._preview.setHtml(html, QUrl(base))
        self._rebuild_outline(text)

    def _rebuild_outline(self, text: str) -> None:
        self._outline.clear()
        for level, heading, slug in mdrender.extract_outline(text):
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
    def _reload_favorites(self) -> None:
        self._fav_list.clear()
        for path in self._favorites:
            item = QListWidgetItem(os.path.basename(path))
            item.setToolTip(path)
            item.setData(int(Qt.ItemDataRole.UserRole), path)
            self._fav_list.addItem(item)

    def _on_favorite_activated(self, item: QListWidgetItem) -> None:
        self._open_file(item.data(int(Qt.ItemDataRole.UserRole)))

    def _toggle_favorite(self, path: str) -> None:
        if path in self._favorites:
            self._favorites.remove(path)
        elif len(self._favorites) < MAX_FAVORITES:
            self._favorites.append(path)
        else:
            QMessageBox.information(
                self, DISPLAY_NAME,
                f"Favorites are limited to {MAX_FAVORITES}.",
            )
            return
        update_config({"favorites": self._favorites})
        self._reload_favorites()

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
                fav = ("Remove favorite" if path in self._favorites
                       else "Add favorite")
                menu.addAction(fav, lambda: self._toggle_favorite(path))
            menu.addAction("New file…", lambda: self._new_in(path, kind))
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

    def _new_in(self, path: str, kind: str) -> None:
        folder = self._dir_of(path, kind)
        name, ok = QInputDialog.getText(self, "New file", "File name:",
                                        text="untitled.md")
        if not ok or not name.strip():
            return
        if os.path.splitext(name)[1] == "":
            name += ".md"
        target = os.path.join(folder, name)
        try:
            with open(target, "x", encoding="utf-8") as fh:
                fh.write(f"# {os.path.splitext(name)[0]}\n\n")
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
                text, base, self._gh_css, self._pyg_css, self._mermaid_js,
                title=f"{DISPLAY_NAME} Help",
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
        answer = QMessageBox.question(
            self, DISPLAY_NAME,
            f"MDBoss v{info['version_str']} is available "
            f"(you have v{APP_VERSION}).\n\nOpen the download page?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer == QMessageBox.StandardButton.Yes:
            webbrowser.open(info.get("html_url", RELEASES_URL))

    def _on_update_failed(self, message: str) -> None:
        if self._update_manual:
            QMessageBox.warning(
                self, DISPLAY_NAME, f"Update check failed:\n{message}"
            )

    # ---- Geometry persistence ------------------------------------------- #
    def _restore_geometry(self, cfg: ConfigDict) -> None:
        geo = cfg.get("geometry")
        if isinstance(geo, str):
            self.restoreGeometry(QByteArray.fromBase64(geo.encode("ascii")))
        for key, split in (("split_main", self._main_split),
                           ("split_right", self._right),
                           ("split_mid", self._mid)):
            state = cfg.get(key)
            if isinstance(state, str):
                split.restoreState(
                    QByteArray.fromBase64(state.encode("ascii"))
                )

    def closeEvent(  # noqa: N802 (Qt override)
        self, event: QCloseEvent
    ) -> None:
        if not self._confirm_discard():
            event.ignore()
            return
        update_config({
            "roots": self._roots,
            "favorites": self._favorites,
            "geometry": _b64(self.saveGeometry()),
            "split_main": _b64(self._main_split.saveState()),
            "split_right": _b64(self._right.saveState()),
            "split_mid": _b64(self._mid.saveState()),
        })
        event.accept()


def main() -> None:
    """Create the application and show the main window."""
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    window = MainWindow()
    window.show()
    # Open a file passed on the command line, if any.
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        window._open_file(os.path.abspath(sys.argv[1]))  # noqa: SLF001
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
