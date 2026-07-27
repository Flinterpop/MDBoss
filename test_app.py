"""Tests for pure helpers in ``app`` that need no running Qt application."""

from __future__ import annotations

import datetime
import json
import os
import winreg
from pathlib import Path

import pytest

import app


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


def test_portable_batch_swaps_exe() -> None:
    batch = app._portable_batch(
        r"C:\t\new.exe", r"C:\a\MDBoss.exe", [r"C:\t\up.zip"]
    )
    assert r'move /y "C:\t\new.exe" "C:\a\MDBoss.exe"' in batch
    assert r'start "" "C:\a\MDBoss.exe"' in batch
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
            {"name": "MDBoss-Portable.zip", "browser_download_url": "u/zip"},
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
