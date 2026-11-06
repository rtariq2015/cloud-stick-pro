# Bundled license texts

`qt-6.10.3/` contains the union of the standard license-text files in the
`LICENSES` directories of Qt Base, Qt WebEngine and PySide6 at their `v6.10.3`
tags, plus Qt WebEngine's top-level Chromium notice. The included `SOURCES.json`
records the source URL and SHA256 of each copied file.

`pyserial-3.5/` supplies the upstream BSD notice omitted by the wheel metadata.
`cpython-3.12/` supplies the Python notice, initially from CPython `v3.12.3`.
The packaging step adds the actual build interpreter's license file when
available as `LICENSE.build-interpreter.txt` and copies installed helper/PyInstaller distribution
license files into `python-distributions/`.

These notices accompany the dynamically linked bundle. Qt WebEngine's complete
component-specific third-party attributions are in its corresponding Chromium
source snapshot, linked in `THIRD_PARTY_NOTICES.md`; standard SPDX texts alone
do not enumerate those components. Preserve the exact source snapshot and
component notices as part of a public redistribution review.
