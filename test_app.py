"""Tests for pure helpers in ``app`` that need no running Qt application."""

from __future__ import annotations

import datetime
import json
import os
import sys
import zipfile
from pathlib import Path

import pytest

import app

# The registry round-trip is the only Windows-only test; the registration plan
# itself is pure and is checked everywhere.
IS_WINDOWS = sys.platform.startswith("win")
if IS_WINDOWS:
    import winreg

windows_only = pytest.mark.skipif(
    not IS_WINDOWS, reason="Windows file associations"
)


def test_md_counts_recursive(tmp_path: Path) -> None:
    (tmp_path / "a.md").write_text("x", encoding="utf-8")
    sub = tmp_path / "sub"
    sub.mkdir()
    (sub / "b.md").write_text("x", encoding="utf-8")
    (sub / "c.markdown").write_text("x", encoding="utf-8")
    (sub / "ignore.txt").write_text("x", encoding="utf-8")
    deep = sub / "deep"
    deep.mkdir()
    (deep / "d.md").write_text("x", encoding="utf-8")

    counts = app.md_counts_for_root(str(tmp_path))
    assert counts[app._norm(str(deep))] == 1        # just d.md
    assert counts[app._norm(str(sub))] == 3         # b, c, deep/d
    assert counts[app._norm(str(tmp_path))] == 4    # a + sub subtree


def test_md_counts_empty_folder(tmp_path: Path) -> None:
    (tmp_path / "empty").mkdir()
    counts = app.md_counts_for_root(str(tmp_path))
    assert counts[app._norm(str(tmp_path / "empty"))] == 0
    assert counts[app._norm(str(tmp_path))] == 0


def test_push_recent_newest_first_and_capped() -> None:
    recents: list[str] = []
    for name in ("a", "b", "c", "d", "e", "f", "g"):
        recents = app.push_recent(recents, rf"C:\docs\{name}.md")
    assert len(recents) == app.MAX_RECENTS
    assert [os.path.basename(p) for p in recents] == [
        "g.md", "f.md", "e.md", "d.md", "c.md", "b.md"
    ]                                          # a.md fell off the end


def test_push_recent_promotes_instead_of_duplicating() -> None:
    recents = app.push_recent(
        app.push_recent([], r"C:\docs\a.md"), r"C:\docs\b.md"
    )
    recents = app.push_recent(recents, r"C:\DOCS\A.MD")   # same file, any case
    assert len(recents) == 2
    assert os.path.basename(recents[0]).lower() == "a.md"


def test_push_recent_stores_absolute_paths(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.chdir(tmp_path)
    recents = app.push_recent([], "note.md")
    assert os.path.isabs(recents[0])
    assert os.path.basename(recents[0]) == "note.md"


def test_cli_path_takes_first_argument() -> None:
    assert app.cli_path(["MDBoss.exe", r"C:\docs\a note.md"]) == (
        r"C:\docs\a note.md"                   # spaces arrive as one argv entry
    )


def test_cli_path_none_without_arguments() -> None:
    assert app.cli_path(["MDBoss.exe"]) is None
    assert app.cli_path([]) is None


def test_cli_path_ignores_qt_switches() -> None:
    assert app.cli_path(["MDBoss.exe", "-platform", "windows"]) is None
    assert app.cli_path(["MDBoss.exe", ""]) is None


def test_sanitize_paths_filters_and_caps() -> None:
    assert app.sanitize_paths(["a", 3, None, "b", "c"], 2) == ["a", "b"]
    assert app.sanitize_paths("not a list", 5) == []
    assert app.sanitize_paths(None, 5) == []


def test_apply_template_placeholders() -> None:
    out = app.apply_template("# {{title}}\nOn {{date}}.", "My Doc")
    assert "# My Doc" in out
    assert datetime.datetime.now().strftime("%Y-%m-%d") in out
    assert "{{title}}" not in out


def test_seed_and_list_templates(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("APPDATA", str(tmp_path))
    assert app.list_templates() == []              # nothing seeded yet
    app.seed_templates()
    names = [name for name, _path in app.list_templates()]
    assert "Document" in names
    assert "Meeting Notes" in names
    body = Path(app.templates_dir(), "Document.md").read_text(encoding="utf-8")
    assert "{{title}}" in body                     # placeholders preserved
    # Seeding is idempotent: a second call does not error or duplicate.
    app.seed_templates()
    assert len(app.list_templates()) == 2


def test_list_templates_ignores_non_markdown(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("APPDATA", str(tmp_path))
    os.makedirs(app.templates_dir())
    Path(app.templates_dir(), "ok.md").write_text("x", encoding="utf-8")
    Path(app.templates_dir(), "note.txt").write_text("x", encoding="utf-8")
    assert [n for n, _ in app.list_templates()] == ["ok"]


def test_find_inbox_as_subfolder(tmp_path: Path) -> None:
    inbox = tmp_path / "MD_Inbox"
    inbox.mkdir()
    roots = [{"name": "Docs", "path": str(tmp_path)}]
    assert app.find_inbox(roots) == str(inbox)


def test_find_inbox_root_named_inbox(tmp_path: Path) -> None:
    inbox = tmp_path / "md_inbox"           # case-insensitive match
    inbox.mkdir()
    roots = [{"name": "Inbox", "path": str(inbox)}]
    assert app.find_inbox(roots) == str(inbox)


def test_find_inbox_absent(tmp_path: Path) -> None:
    (tmp_path / "notes").mkdir()
    roots = [{"name": "Docs", "path": str(tmp_path)}]
    assert app.find_inbox(roots) is None


def test_find_inbox_no_roots() -> None:
    assert app.find_inbox([]) is None


def test_unique_dest_no_collision(tmp_path: Path) -> None:
    dest = app.unique_dest(str(tmp_path), "note.md")
    assert dest == str(tmp_path / "note.md")


def test_unique_dest_collision_increments(tmp_path: Path) -> None:
    (tmp_path / "note.md").write_text("x", encoding="utf-8")
    assert app.unique_dest(str(tmp_path), "note.md") == str(
        tmp_path / "note (2).md"
    )
    (tmp_path / "note (2).md").write_text("x", encoding="utf-8")
    assert app.unique_dest(str(tmp_path), "note.md") == str(
        tmp_path / "note (3).md"
    )


def _plan() -> app.RegPlan:
    return app.registration_plan(
        r'"C:\a\MDBoss.exe" "%1"', r"C:\a\MDBoss.exe,0", "MDBoss.exe"
    )


def test_registration_plan_covers_every_markdown_extension() -> None:
    plan = _plan()
    for ext in app.MARKDOWN_EXTS:
        key = rf"Software\Classes\{ext}\OpenWithProgids"
        assert (key, app.PROGID, "") in plan["values"]
        assert (rf"Software\{app.APP_NAME}\Capabilities\FileAssociations",
                ext, app.PROGID) in plan["values"]


def test_registration_plan_open_command_passes_the_file() -> None:
    plan = _plan()
    command = [
        data for key, name, data in plan["values"]
        if key.endswith(rf"{app.PROGID}\shell\open\command") and name == ""
    ]
    assert command == [r'"C:\a\MDBoss.exe" "%1"']    # quoted, %1 preserved


def test_registration_plan_requires_a_file_placeholder() -> None:
    with pytest.raises(AssertionError):
        app.registration_plan(r'"C:\a\MDBoss.exe"', "icon", "MDBoss.exe")


def test_registration_plan_never_deletes_shared_keys() -> None:
    """OpenWithProgids and RegisteredApplications belong to every app."""
    plan = _plan()
    for key in plan["owned_keys"]:
        assert "OpenWithProgids" not in key
        assert "RegisteredApplications" not in key
    shared_keys = {key for key, _name in plan["shared_values"]}
    assert r"Software\RegisteredApplications" in shared_keys
    assert all(
        rf"Software\Classes\{ext}\OpenWithProgids" in shared_keys
        for ext in app.MARKDOWN_EXTS
    )


def test_registration_plan_deletes_children_before_parents() -> None:
    """RegDeleteKey fails on a key that still has subkeys."""
    owned = _plan()["owned_keys"]
    for i, key in enumerate(owned):
        for later in owned[i + 1:]:
            assert not later.startswith(key + "\\"), (
                f"{later} must be deleted before its parent {key}"
            )


@windows_only
def test_registration_plan_round_trips_in_the_registry(
    monkeypatch: pytest.MonkeyPatch
) -> None:
    """Register and unregister for real, under throwaway names.

    Everything the plan touches is renamed first, so running the tests cannot
    disturb a genuine MD Boss registration on the same machine.
    """
    monkeypatch.setattr(app, "PROGID", "MDBossTest.Markdown")
    monkeypatch.setattr(app, "DISPLAY_NAME", "MD Boss Test")
    monkeypatch.setattr(
        app, "CAPABILITIES_SUBKEY", r"Software\MDBossTest\Capabilities"
    )
    exe = r"C:\nowhere\MDBossTest.exe"
    command = f'"{exe}" "%1"'
    plan = app.registration_plan(command, f"{exe},0", "MDBossTest.exe")

    # A decoy from "another application" must survive our unregister.
    shared = r"Software\Classes\.md\OpenWithProgids"
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, shared) as handle:
        winreg.SetValueEx(handle, "Decoy.Markdown", 0, winreg.REG_SZ, "")

    app.apply_registration(plan)
    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            rf"Software\Classes\{app.PROGID}\shell\open\command",
        ) as handle:
            assert winreg.QueryValueEx(handle, "")[0] == command
        assert app.is_registered(command)
        assert not app.is_registered(r'"C:\other.exe" "%1"')
    finally:
        app.remove_registration(plan)

    assert not app.is_registered(command)
    with pytest.raises(OSError):                    # our own key is gone
        winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                       rf"Software\Classes\{app.PROGID}")
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, shared, 0,
                        winreg.KEY_READ | winreg.KEY_SET_VALUE) as handle:
        assert winreg.QueryValueEx(handle, "Decoy.Markdown")[0] == ""
        with pytest.raises(OSError):                # ours, and only ours, went
            winreg.QueryValueEx(handle, app.PROGID)
        winreg.DeleteValue(handle, "Decoy.Markdown")


def test_installer_batch_installs_and_relaunches() -> None:
    batch = app._installer_batch(r"C:\t\MDBoss-Setup.exe", r"C:\a\MDBoss.exe")
    assert "/VERYSILENT /NORESTART /SUPPRESSMSGBOXES" in batch
    assert r'"C:\t\MDBoss-Setup.exe"' in batch       # runs the installer
    assert r'start "" "C:\a\MDBoss.exe"' in batch     # relaunches the app
    assert 'del /q "%~f0"' in batch                   # self-cleanup
    # Waits for every MDBoss.exe to exit before installing (not a fixed delay).
    assert 'tasklist /FI "IMAGENAME eq MDBoss.exe"' in batch
    assert "goto mdwait" in batch


def test_portable_batch_copies_the_tree_over_the_install() -> None:
    batch = app._portable_batch(
        r"C:\t\up.zip.new\MDBoss", r"C:\a\MDBoss.exe", [r"C:\t\up.zip"],
        r"C:\t\up.zip.new",
    )
    # One-dir: copy the tree into the app folder, never move a single exe.
    assert r'robocopy "C:\t\up.zip.new\MDBoss" "C:\a"' in batch
    assert "move /y" not in batch
    assert r'start "" "C:\a\MDBoss.exe"' in batch
    assert r'rd /s /q "C:\t\up.zip.new"' in batch      # whole staging tree
    assert r'del /q "C:\t\up.zip"' in batch
    assert 'tasklist /FI "IMAGENAME eq MDBoss.exe"' in batch


def test_translate_windows_path_reanchors_known_folder() -> None:
    home = os.path.expanduser("~")
    out = app.translate_windows_path(r"J:\Dropbox\03_Work\note.md")
    assert out == os.path.join(home, "Dropbox", "03_Work", "note.md")
    out = app.translate_windows_path(r"C:\Users\me\Documents\a\b.md")
    assert out == os.path.join(home, "Documents", "a", "b.md")


def test_translate_windows_path_leaves_posix_untouched() -> None:
    for p in ("/home/me/notes/a.md", "relative/note.md", "note.md"):
        assert app.translate_windows_path(p) == p


def test_translate_windows_path_unknown_root_becomes_absolute() -> None:
    out = app.translate_windows_path(r"D:\Projects\x\y.md")
    assert out == "/Projects/x/y.md"


def test_running_appimage_detects_env(tmp_path: Path, monkeypatch) -> None:
    fake = tmp_path / "MDBoss-x86_64.AppImage"
    fake.write_bytes(b"\x7fELF")
    monkeypatch.setenv("APPIMAGE", str(fake))
    assert app.running_appimage() == str(fake)
    monkeypatch.setenv("APPIMAGE", str(tmp_path / "does-not-exist.AppImage"))
    assert app.running_appimage() is None
    monkeypatch.delenv("APPIMAGE", raising=False)
    assert app.running_appimage() is None


def test_fetch_latest_release_parses_appimage_asset(monkeypatch) -> None:
    import io
    import urllib.request

    payload = {
        "tag_name": "v0.2.0",
        "html_url": "https://example/rel",
        "assets": [
            {"name": "MDBoss-Setup.exe", "browser_download_url": "u/exe"},
            {"name": app.UPDATE_PORTABLE_ASSET_NAME,
             "browser_download_url": "u/zip"},
            # The pre-one-dir name, still published on old releases: it must
            # not be picked up, or a portable copy would install a bare exe.
            {"name": "MDBoss-Portable.zip", "browser_download_url": "u/old"},
            {"name": "MDBoss-x86_64.AppImage", "browser_download_url": "u/aim"},
            {"name": "MDBoss-x86_64.AppImage.zsync",
             "browser_download_url": "u/zsync"},
        ],
    }

    class _Resp(io.BytesIO):
        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

    monkeypatch.setattr(
        urllib.request, "urlopen",
        lambda *a, **k: _Resp(json.dumps(payload).encode()),
    )
    info = app.fetch_latest_release()
    assert info["version"] == (0, 2, 0)
    assert info["appimage_url"] == "u/aim"
    assert info["asset_url"] == "u/exe"
    assert info["portable_url"] == "u/zip"


def test_portable_batch_relaunches_even_if_the_copy_fails() -> None:
    """A half-done update must still leave a running app, not a brick."""
    batch = app._portable_batch(r"C:\t\new", r"C:\a\MDBoss.exe", [])
    lines = [ln for ln in batch.splitlines() if ln.strip()]
    copy_at = next(i for i, ln in enumerate(lines) if ln.startswith("robocopy"))
    start_at = next(i for i, ln in enumerate(lines) if ln.startswith("start "))
    assert copy_at < start_at                  # copy first, then relaunch
    assert not any("exit" in ln or "if errorlevel" in ln
                   for ln in lines[copy_at:start_at])   # nothing can abort it


def test_portable_asset_is_not_the_pre_one_dir_name() -> None:
    """v0.1.11 and earlier match "MDBoss-Portable.zip" and then move the first
    .exe in it over their own -- for a one-dir zip, the stub without its
    _internal folder, leaving an app that cannot start.  Publishing the new
    layout under the old name would break every portable copy in the field."""
    assert app.UPDATE_PORTABLE_ASSET_NAME != "MDBoss-Portable.zip"
    assert app.UPDATE_PORTABLE_ASSET_NAME.endswith(".zip")


def test_extract_portable_finds_exe_in_a_top_level_folder(
    tmp_path: Path
) -> None:
    zip_path = tmp_path / "up.zip"
    with zipfile.ZipFile(zip_path, "w") as archive:
        archive.writestr("MDBoss/MDBoss.exe", "exe")
        archive.writestr("MDBoss/_internal/base_library.zip", "lib")
    dest = tmp_path / "staging"
    source = app.extract_portable(str(zip_path), str(dest))
    assert source == str(dest / "MDBoss")
    assert (dest / "MDBoss" / "_internal" / "base_library.zip").is_file()


def test_extract_portable_finds_exe_at_the_zip_root(tmp_path: Path) -> None:
    zip_path = tmp_path / "up.zip"
    with zipfile.ZipFile(zip_path, "w") as archive:
        archive.writestr("MDBoss.exe", "exe")
    dest = tmp_path / "staging"
    assert app.extract_portable(str(zip_path), str(dest)) == str(dest)


def test_extract_portable_rejects_a_zip_without_the_exe(tmp_path: Path) -> None:
    zip_path = tmp_path / "up.zip"
    with zipfile.ZipFile(zip_path, "w") as archive:
        archive.writestr("readme.txt", "nope")
    with pytest.raises(ValueError):
        app.extract_portable(str(zip_path), str(tmp_path / "staging"))
