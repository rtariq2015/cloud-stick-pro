#!/usr/bin/env python3
"""Install an extracted portable bundle for the current user only."""

from pathlib import Path
import argparse
import os
import shlex
import shutil
import sys

APP_ID = "cloud-stick-desktop"


def desktop_quote(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("`", "\\`").replace("$", "\\$").replace("%", "%%") + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path.home() / ".local" / "opt" / APP_ID)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    prefix = args.prefix.expanduser().resolve()
    app = source / "cloud-stick"
    if not (app / "cloud-stick").is_file():
        parser.error("Run this installer from the extracted Cloud Stick Setup release directory")
    if os.geteuid() == 0:
        parser.error("Run this installer as your desktop user")
    if prefix.exists():
        parser.error(f"{prefix} already exists; run its uninstall-user.py before replacing it")
    bindir = Path.home() / ".local" / "bin"
    datadir = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    launcher = bindir / "cloud-stick"
    desktop_file = datadir / "applications" / f"{APP_ID}.desktop"
    icon = datadir / "icons" / "hicolor" / "scalable" / "apps" / f"{APP_ID}.svg"
    for path in (launcher, desktop_file, icon):
        if path.exists() or path.is_symlink():
            parser.error(f"Refusing to overwrite existing file: {path}")
    prefix.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(app, prefix, symlinks=True)
    (prefix / ".cloud-stick-user-install").write_text(APP_ID + "\n", encoding="utf-8")
    shutil.copy2(source / "uninstall-user.py", prefix / "uninstall-user.py")
    for path in (launcher, desktop_file, icon):
        path.parent.mkdir(parents=True, exist_ok=True)
    launcher.write_text("#!/bin/sh\nexec " + shlex.quote(str(prefix / "cloud-stick")) + ' "$@"\n', encoding="utf-8")
    launcher.chmod(0o755)
    shutil.copy2(source / "cloud-stick.svg", icon)
    desktop_file.write_text(
        "[Desktop Entry]\nType=Application\nName=Cloud Stick Setup\n"
        "Comment=Set up and inspect Cloud Stick Pro\n"
        f"Exec={desktop_quote(str(prefix / 'cloud-stick'))}\n"
        f"Icon={icon}\nTerminal=false\nCategories=Utility;\nStartupNotify=true\nStartupWMClass=Cloud Stick Setup\n",
        encoding="utf-8",
    )
    print(f"Installed Cloud Stick Setup in {prefix}")
    print("Launch Cloud Stick Setup from the applications menu, or run " + str(launcher))
    return 0


if __name__ == "__main__":
    sys.exit(main())

