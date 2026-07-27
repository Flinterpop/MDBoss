# -*- mode: python ; coding: utf-8 -*-
# One-dir, windowed build of MDBoss: dist\MDBoss\MDBoss.exe plus _internal\.
# PySide6's PyInstaller hooks collect QtWebEngine automatically
# (QtWebEngineProcess, .pak resources, locales); the bundled render assets and
# help doc are added via `datas`.
#
# One-dir rather than one-file because MD Boss registers as the Windows handler
# for .md: a one-file build unpacks its whole ~230 MB payload into %TEMP% on
# every launch, which is paid once per double-clicked document.  One-dir loads
# from disk and starts immediately.  The portable zip therefore ships a folder,
# and the updater copies a tree over the old one (see _portable_batch).


a = Analysis(
    ['app.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('mdboss.ico', '.'),
        ('HELP.md', '.'),
        ('assets', 'assets'),
    ],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='MDBoss',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['mdboss.ico'],
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='MDBoss',
)
