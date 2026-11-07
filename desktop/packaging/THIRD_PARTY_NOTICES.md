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



## Independent firmware installer (0.6.0)

The `firmware-helper` program is built separately from the Qt GUI and ships with
`installer-source.zip`, the preferred Python source for its wrapper, unmodified
esptool/esp-pylib source, build recipe and pinned dependencies. The new installer
engine, worker and installer-only USB helper are GPL-3.0-or-later. The five newly
authored shared modules explicitly carrying MIT identifiers have the scoped MIT
notice under `licenses/firmware-installer`. These grants do not relicense the
original firmware, artwork, existing frontend or other components.

| Added component | Exact pin | Terms / primary source |
| --- | --- | --- |
| esptool | 5.4.0 | GPL-2.0-or-later; [official source](https://github.com/espressif/esptool/tree/v5.4.0) |
| esp-pylib | 1.2.1 | Apache-2.0; installed distribution notice/source included |
| cryptography | 50.0.2 | Apache-2.0 OR BSD-3-Clause; [source](https://github.com/pyca/cryptography) |
| cffi / pycparser | 2.1.1 / 3.12 | MIT-0 / BSD-3-Clause |
| bitstring / tibs | 5.0.0 / 2.0.2 | MIT |
| reedsolo / intelhex | 1.7.0 / 2.3.0 | Public Domain / BSD |
| PyYAML | 6.0.3 | MIT |
| rich-click / rich | 1.9.9 / 15.0.0 | MIT |
| click / Pygments | 8.5.0 / 2.21.0 | BSD-3-Clause / BSD-2-Clause |
| markdown-it-py / mdurl | 4.2.0 / 0.1.2 | MIT |
| websockets / colorama (Windows) | 17.4 / 0.4.6 | BSD-3-Clause / BSD-3-Clause |

The packaging step copies the actual distributions' LICENSE/COPYING/NOTICE files.
Keep all notices, helper source and the complete app folder when redistributing.
The initial firmware package remains a distinct data asset with the project's
retained source/attribution release gate in `docs/CREDITS.md`. The manufacturer's
stock binary is not redistributed; only its public recognition hashes are used.
