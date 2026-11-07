# -*- mode: python ; coding: utf-8 -*-
"""Independent GPL installer, aggregated beside the desktop GUI."""
from pathlib import Path
from PyInstaller.utils.hooks import collect_submodules, copy_metadata
root = Path(SPECPATH).parent
hidden = collect_submodules('esptool') + ['serial.tools.list_ports', '_cffi_backend']
a = Analysis([str(root / 'packaging/firmware-entrypoint.py')], pathex=[str(root)],
             binaries=[], datas=copy_metadata('esptool') + copy_metadata('esp-pylib'),
             hiddenimports=hidden, excludes=['PySide6','PyQt5','PyQt6','bleak','tkinter'],
             noarchive=False, optimize=0)
pyz = PYZ(a.pure)
exe = EXE(pyz,a.scripts,[],exclude_binaries=True,name='cloud-stick-installer',
          debug=False,strip=False,upx=False,console=False,contents_directory='_internal')
coll = COLLECT(exe,a.binaries,a.datas,strip=False,upx=False,name='cloud-stick-installer')
