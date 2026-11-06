#!/usr/bin/env python3
"""Remove only a Cloud Stick Setup installation made by install-user.py."""

from pathlib import Path
import os
import shutil
import sys


def main():
    prefix = Path(__file__).resolve().parent
    marker = prefix / ".cloud-stick-user-install"
    if os.geteuid() == 0 or not marker.is_file() or marker.read_text(encoding="utf-8").strip() != "cloud-stick-desktop":
        raise SystemExit("Run the uninstall-user.py inside your user installation as your desktop user")
    datadir = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    launcher = Path.home() / ".local" / "bin" / "cloud-stick"
    desktop_file = datadir / "applications" / "cloud-stick-desktop.desktop"
    icon = datadir / "icons" / "hicolor" / "scalable" / "apps" / "cloud-stick-desktop.svg"
    if launcher.is_file() and str(prefix / "cloud-stick") in launcher.read_text(encoding="utf-8"):
        launcher.unlink()
    if desktop_file.is_file() and str(prefix / "cloud-stick") in desktop_file.read_text(encoding="utf-8"):
        desktop_file.unlink()
        if icon.is_file():
            icon.unlink()
    shutil.rmtree(prefix)
    print("Removed Cloud Stick Setup application files. User preferences were retained.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

