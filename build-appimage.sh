#!/usr/bin/env bash
#
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

# -- 7. appimagetool -------------------------------------------------------- #
TOOL="$BUILD/appimagetool-${ARCH}.AppImage"
if [[ ! -x "$TOOL" ]]; then
    echo ">> Fetching appimagetool ..."
    wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
        -O "$TOOL"
    chmod +x "$TOOL"
fi

echo ">> Packaging ..."
rm -f "$OUT"
env APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$ARCH" "$TOOL" "$APPDIR" "$OUT"

echo ">> Done: $OUT ($(du -h "$OUT" | cut -f1))"
