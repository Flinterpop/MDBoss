#!/usr/bin/env bash
#
# DEPRECATED -- builds the Python app for Linux.  The Python app was deprecated
# in v1.2.2 and Linux is no longer a supported target; the C++ Windows app in
# MDBossCpp/ is the only thing that ships.  Kept as historical reference and
# guarded off below.  Its output must never be published: the AppImage asset
# name is what old installs poll for via the .zsync manifest.
#
# To run it anyway (historical rebuild only, never publish):
#     MDBOSS_BUILD_DEPRECATED_PYTHON=1 ./build-appimage.sh
#
if [ -z "${MDBOSS_BUILD_DEPRECATED_PYTHON:-}" ]; then
    echo "build-appimage.sh is DEPRECATED and did not run." >&2
    echo "The Python app was deprecated in v1.2.2 and Linux is no longer supported;" >&2
    echo "MDBossCpp (Windows) is what ships. Release with: .\\release.ps1 <version>" >&2
    echo "To build the dead AppImage locally anyway (never publish it):" >&2
    echo "  MDBOSS_BUILD_DEPRECATED_PYTHON=1 ./build-appimage.sh" >&2
    exit 1
fi

# Build a self-contained Linux AppImage of MD Boss (x86_64).
#
# The AppImage bundles a relocatable CPython (python-build-standalone), the
# app's PySide6 + QtWebEngine dependencies, and the app source, so it runs on
# any modern x86_64 Linux desktop without Python or Qt installed.  The Linux
# counterpart of release.ps1's PyInstaller build.
#
#     ./build-appimage.sh                 # -> dist/MDBoss-x86_64.AppImage
#
# Requirements: bash, wget, mksquashfs (squashfs-tools).  `gh` (authenticated)
# is used to resolve the latest python-build-standalone build; override with
# PBS_URL=... to pin or to build without gh.  No FUSE needed -- appimagetool
# is run via its extract-and-run mode.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build/appimage"
APPDIR="$BUILD/AppDir"
DIST="$HERE/dist"
PYVER="3.12"
ARCH="x86_64"

VERSION="$(sed -n 's/^APP_VERSION *= *"\([^"]*\)".*/\1/p' "$HERE/app.py" | head -1)"
[[ -n "$VERSION" ]] || { echo "Could not read APP_VERSION from app.py" >&2; exit 1; }
OUT="$DIST/MDBoss-${ARCH}.AppImage"

echo ">> Building MD Boss v$VERSION AppImage ($ARCH)"
rm -rf "$APPDIR"
mkdir -p "$BUILD" "$APPDIR/usr" "$DIST"

# -- 1. Relocatable Python -------------------------------------------------- #
if [[ -z "${PBS_URL:-}" ]]; then
    command -v gh >/dev/null || { echo "Need gh, or set PBS_URL=..." >&2; exit 1; }
    echo ">> Resolving latest python-build-standalone $PYVER ..."
    PBS_URL="$(gh api repos/astral-sh/python-build-standalone/releases/latest \
        --jq ".assets[] | select(.name | test(\"cpython-${PYVER}\\\\.[0-9]+.*${ARCH}-unknown-linux-gnu-install_only\\\\.tar\\\\.gz$\")) | .browser_download_url" \
        | grep -v debug | head -1)"
fi
[[ -n "$PBS_URL" ]] || { echo "Could not resolve a Python build" >&2; exit 1; }
echo ">> Python: $PBS_URL"
wget -q "$PBS_URL" -O "$BUILD/python.tar.gz"
tar xf "$BUILD/python.tar.gz" -C "$BUILD"
mv "$BUILD"/python/* "$APPDIR/usr/"
rmdir "$BUILD/python"
PY="$APPDIR/usr/bin/python${PYVER}"

# -- 2. Dependencies -------------------------------------------------------- #
echo ">> Installing dependencies into the AppDir ..."
"$PY" -m pip install --no-cache-dir -q --upgrade pip
"$PY" -m pip install --no-cache-dir -q -r "$HERE/requirements.txt"

# -- 3. App source ---------------------------------------------------------- #
APPROOT="$APPDIR/usr/share/mdboss"
mkdir -p "$APPROOT"
cp -r "$HERE/app.py" "$HERE/mdrender.py" "$HERE/assets" "$HERE/mdboss.svg" \
      "$HERE/mdboss.ico" "$HERE/HELP.md" "$HERE/LICENSE" "$APPROOT/"

# -- 4. Icon (rasterise the SVG with the bundled Qt) ------------------------ #
echo ">> Rendering icon ..."
QT_QPA_PLATFORM=offscreen "$PY" - "$HERE/mdboss.svg" "$BUILD/mdboss.png" <<'PY'
import sys
from PySide6.QtGui import QImage, QPainter
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtCore import Qt
src, dst = sys.argv[1], sys.argv[2]
img = QImage(256, 256, QImage.Format.Format_ARGB32)
img.fill(Qt.GlobalColor.transparent)
p = QPainter(img); QSvgRenderer(src).render(p); p.end()
assert img.save(dst), "icon render failed"
PY

# -- 5. AppDir metadata (AppRun, .desktop, icon) ---------------------------- #
cat > "$APPDIR/AppRun" <<EOF
#!/bin/bash
HERE="\$(dirname "\$(readlink -f "\${0}")")"
export PATH="\$HERE/usr/bin:\$PATH"
# QtWebEngine's chrome-sandbox needs a setuid root helper an AppImage can't
# provide, so run the renderer without the sandbox.
export QTWEBENGINE_DISABLE_SANDBOX=1
exec "\$HERE/usr/bin/python${PYVER}" "\$HERE/usr/share/mdboss/app.py" "\$@"
EOF
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/mdboss.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=MD Boss
Comment=Local Markdown manager, editor, and GitHub-style viewer
Exec=mdboss
Icon=mdboss
Categories=Office;Utility;TextEditor;
Terminal=false
MimeType=text/markdown;
EOF
cp "$BUILD/mdboss.png" "$APPDIR/mdboss.png"
cp "$BUILD/mdboss.png" "$APPDIR/.DirIcon"

# -- 6. Trim dead weight ---------------------------------------------------- #
find "$APPDIR/usr" -type d -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
rm -rf "$APPDIR/usr/lib/python${PYVER}/test" \
       "$APPDIR/usr/lib/python${PYVER}/idlelib" \
       "$APPDIR/usr/lib/python${PYVER}/tkinter" \
       "$APPDIR/usr/lib/python${PYVER}/turtledemo" 2>/dev/null || true

# -- 7. appimagetool + zsync ------------------------------------------------ #
TOOL="$BUILD/appimagetool-${ARCH}.AppImage"
if [[ ! -x "$TOOL" ]]; then
    echo ">> Fetching appimagetool ..."
    wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
        -O "$TOOL"
    chmod +x "$TOOL"
fi

# zsyncmake builds the .zsync index that lets AppImageUpdate / appimaged patch
# the AppImage in place.  Use the system binary if present; otherwise borrow
# the one bundled inside the classic AppImageKit appimagetool -- no install.
ZSYNCMAKE="$(command -v zsyncmake || true)"
if [[ -z "$ZSYNCMAKE" ]]; then
    echo ">> Fetching zsyncmake (from AppImageKit) ..."
    AITOOL="$BUILD/appimagekit-${ARCH}.AppImage"
    [[ -x "$AITOOL" ]] || {
        wget -q "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
            -O "$AITOOL" && chmod +x "$AITOOL"; }
    ( cd "$BUILD" && rm -rf squashfs-root \
        && env APPIMAGE_EXTRACT_AND_RUN=1 "$AITOOL" \
             --appimage-extract 'usr/bin/zsyncmake' >/dev/null 2>&1 || true )
    ZSYNCMAKE="$BUILD/squashfs-root/usr/bin/zsyncmake"
    [[ -x "$ZSYNCMAKE" ]] || ZSYNCMAKE=""
fi
# appimagetool only emits the .zsync when zsyncmake is on PATH.
[[ -n "$ZSYNCMAKE" ]] && export PATH="$(dirname "$ZSYNCMAKE"):$PATH"

echo ">> Packaging ..."
rm -f "$OUT" "$OUT.zsync"
# Embed AppImage update information so the app (and external tools) can update
# in place from the GitHub releases.
UPDATE_INFO="gh-releases-zsync|Flinterpop|MDBoss|latest|MDBoss-*${ARCH}.AppImage.zsync"
env APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$ARCH" "$TOOL" -u "$UPDATE_INFO" "$APPDIR" "$OUT"

# Fallback so the build is deterministic regardless of appimagetool's behaviour.
if [[ ! -f "$OUT.zsync" && -n "$ZSYNCMAKE" ]]; then
    "$ZSYNCMAKE" -u "$(basename "$OUT")" -o "$OUT.zsync" "$OUT"
fi

echo ">> Done: $OUT ($(du -h "$OUT" | cut -f1))"
[[ -f "$OUT.zsync" ]] && echo ">> zsync: $OUT.zsync" \
    || echo ">> warning: no .zsync (install zsync for AppImageUpdate delta updates)"
