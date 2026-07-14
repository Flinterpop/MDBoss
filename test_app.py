"""Tests for pure helpers in ``app`` that need no running Qt application."""

from __future__ import annotations

from pathlib import Path

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
