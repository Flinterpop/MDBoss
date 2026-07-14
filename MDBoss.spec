# -*- mode: python ; coding: utf-8 -*-
# One-file, windowed build of MDBoss.  PySide6's PyInstaller hooks collect
# QtWebEngine automatically (QtWebEngineProcess, .pak resources, locales); the
# bundled render assets and help doc are added via `datas`.


a = Analysis(
    ['app.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('mdbossicon.ico', '.'),
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
    a.binaries,
    a.datas,
    [],
    name='MDBoss',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['mdbossicon.ico'],
)
