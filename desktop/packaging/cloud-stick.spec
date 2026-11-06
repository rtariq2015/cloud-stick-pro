# -*- mode: python ; coding: utf-8 -*-
"""Native onedir build. Run PyInstaller on the target operating system."""

from pathlib import Path
import sys

from PyInstaller.utils.hooks import collect_submodules, copy_metadata

desktop = Path(SPECPATH).parent
frontend = desktop / "frontend"
if not (frontend / "index.html").is_file():
    raise SystemExit("Missing desktop/frontend/index.html")
if sys.platform not in ("linux", "win32"):
    raise SystemExit("This release recipe supports Linux and Windows builds only")

# Explicit imports trigger PyInstaller's maintained Qt hooks. The
# PySide6.QtWebEngineCore hook collects QtWebEngineProcess, ICU data, .pak
# resources, locales, dependent Qt libraries, and the generated qt.conf.
# Keep Qt's own layout; runtime hooks arrange its search paths.
hiddenimports = [
    "PySide6.QtWebEngineCore",
    "PySide6.QtWebEngineWidgets",
    "PySide6.QtWebChannel",
    "serial.tools.list_ports",
]
if sys.platform == "linux":
    hiddenimports += collect_submodules("bleak.backends.bluezdbus")
    hiddenimports += collect_submodules("dbus_fast")
else:
    hiddenimports += collect_submodules("bleak.backends.winrt")
    hiddenimports += collect_submodules("winrt")

datas = [
    (str(frontend), "frontend"),
    (str(desktop / "packaging" / "THIRD_PARTY_NOTICES.md"), "."),
    (str(desktop / "packaging" / "licenses"), "licenses"),
]
for distribution in ("PySide6", "PySide6_Addons", "PySide6_Essentials", "shiboken6", "bleak", "pyserial"):
    datas += copy_metadata(distribution)
if sys.platform == "linux":
    datas += copy_metadata("dbus-fast")

a = Analysis(
    [str(desktop / "packaging" / "entrypoint.py")],
    pathex=[str(desktop)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=["PyQt5", "PyQt6", "PySide2", "tkinter"],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="cloud-stick",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    contents_directory="_internal",
)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name="cloud-stick")

