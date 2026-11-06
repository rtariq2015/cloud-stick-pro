"""Fail before building when the native interpreter or dependency pins differ."""

from pathlib import Path
import argparse
import importlib.metadata
import platform
import sys

from packaging.requirements import Requirement


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("linux", "win32"))
    args = parser.parse_args()
    if sys.platform != args.target:
        parser.error("Run PyInstaller on the intended target operating system")
    if platform.python_implementation() != "CPython" or sys.version_info[:2] != (3, 12):
        parser.error("Use CPython 3.12")
    if platform.machine().lower() not in ("x86_64", "amd64"):
        parser.error("Use an x86-64 build host and interpreter")
    failures = []
    requirements = Path(__file__).resolve().parents[1] / "requirements.txt"
    for line in requirements.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        requirement = Requirement(line)
        if requirement.marker is not None and not requirement.marker.evaluate():
            continue
        try:
            installed = importlib.metadata.version(requirement.name)
        except importlib.metadata.PackageNotFoundError:
            failures.append(f"{requirement.name} missing")
            continue
        if installed not in requirement.specifier:
            failures.append(f"{requirement.name} installed {installed}, expected {requirement.specifier}")
    if failures:
        raise SystemExit("Install desktop/requirements.txt in a fresh build environment:\n" + "\n".join(failures))
    print(f"Pinned {args.target} CPython {platform.python_version()} x86-64 build inputs verified")


if __name__ == "__main__":
    main()

