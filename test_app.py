"""Tests for pure helpers in ``app`` that need no running Qt application."""

from __future__ import annotations

import datetime
import os
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
