# Cloud Stick desktop: third-party components

The installed `_internal` directory contains dynamically linked Qt/PySide6
libraries and package metadata. Full standard Qt license texts and the Chromium
top-level notice from the pinned Qt 6.10.3 source are included under
`licenses/qt-6.10.3`. Python/helper license files are under `licenses`, including
those supplied by their installed distributions. Preserve that directory when
redistributing the app.

| Component | Upstream | License |
| --- | --- | --- |
| PySide6 / shiboken6 | https://www.qt.io/qt-for-python | LGPL-3.0 / GPL / commercial; see included metadata |
| Qt / Qt WebEngine | https://doc.qt.io/qt-6/licenses-used-in-qt.html | Qt and bundled Chromium component licenses |
| Bleak | https://github.com/hbldh/bleak | MIT |
| pyserial | https://github.com/pyserial/pyserial | BSD-3-Clause |
| dbus-fast (Linux) | https://github.com/Bluetooth-Devices/dbus-fast | MIT |
| PyWinRT projections (Windows) | https://github.com/pywinrt/pywinrt | MIT |
| CPython | https://www.python.org/ | PSF license and bundled component notices |
| PyInstaller bootloader | https://pyinstaller.org/ | GPL with bootloader distribution exception |

Exact installed versions and the build platform are recorded in the artifact's
`build-manifest.json`. Qt WebEngine's Chromium notices are also available in
the upstream Qt source release corresponding to Qt 6.10.3:
https://download.qt.io/official_releases/qt/6.10/6.10.3/submodules/ .

The corresponding Chromium source snapshot used by Qt 6.10.3 is
https://github.com/qt/qtwebengine-chromium/tree/b09162cfb22f1cce78638818e5ebc7cc778a7e43 .
It contains the component-specific Chromium attributions in addition to the
top-level BSD notice. The source URLs and hashes for copied license texts are
recorded in `licenses/SOURCES.json` and `licenses/qt-6.10.3/SOURCES.json`.

