#!/usr/bin/env bash
#
# MDBoss launcher for Linux / macOS.
#
# On first run it creates a local virtual environment (.venv) beside this
# script and installs the dependencies from requirements.txt (PySide6, the
# markdown-it stack, Pygments, Send2Trash).  Every run after that just
# launches the app.  Pass a Markdown file or folder to open it:
#
#     ./run.sh                 # open with your saved root folders
#     ./run.sh ~/Notes/todo.md # open a file
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$HERE/.venv"
PY="$VENV/bin/python"

BOOT_PY="$(command -v python3 || command -v python || true)"
if [[ -z "$BOOT_PY" ]]; then
    echo "Error: python3 is not installed." >&2
    exit 1
fi

# Create the virtual environment on first run (or if it was deleted).
if [[ ! -x "$PY" ]]; then
    echo "First run: creating virtual environment in .venv ..."
    "$BOOT_PY" -m venv "$VENV"
    "$PY" -m pip install --upgrade pip >/dev/null
    echo "Installing dependencies (PySide6 + markdown stack) ..."
    "$PY" -m pip install -r "$HERE/requirements.txt"
fi

# QtWebEngine (the preview) needs a handful of system libraries.  Give a clear
# hint if it can't load rather than a raw traceback.
if ! "$PY" -c 'from PySide6.QtWebEngineWidgets import QWebEngineView' \
        >/dev/null 2>&1; then
    echo "Warning: Qt WebEngine failed to import." >&2
    echo "  It needs system libraries such as libnss3 and libxkbcommon." >&2
    echo "  Debian/Ubuntu : sudo apt install libnss3 libxkbcommon0 libxcomposite1 libxdamage1 libasound2t64" >&2
    echo "  Fedora        : sudo dnf install nss libxkbcommon libXcomposite libXdamage alsa-lib" >&2
fi

exec "$PY" "$HERE/app.py" "$@"
