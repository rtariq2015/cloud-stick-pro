"""Package an existing native PyInstaller onedir bundle; never installs it."""

from pathlib import Path
import argparse
import gzip
import hashlib
import importlib.metadata
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib
import zipfile

from verify_bundle import verify

DESKTOP = Path(__file__).resolve().parents[1]


def version():
    with (DESKTOP / "pyproject.toml").open("rb") as file:
        return tomllib.load(file)["project"]["version"]


def manifest(bundle):
    packages = {dist.metadata["Name"]: dist.version for dist in importlib.metadata.distributions()}
    result = {
        "application": "Cloud Stick Setup",
        "version": version(),
        "python": platform.python_version(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "libc": platform.libc_ver(),
        "source_date_epoch": int(os.environ.get("SOURCE_DATE_EPOCH", "0")),
        "packages": dict(sorted(packages.items(), key=lambda item: item[0].lower())),
        "firmware_flashing": True,
        "automatic_app_updates": False,
        "signed_app_update_checks": True,
        "confirmed_app_update_installation": True,
    }
    try:
        result["source_commit"] = subprocess.check_output(
            ["git", "-C", str(DESKTOP), "rev-parse", "HEAD"], text=True).strip()
        result["desktop_source_dirty"] = bool(subprocess.check_output(
            ["git", "-C", str(DESKTOP), "status", "--porcelain", "--", "."], text=True).strip())
    except (OSError, subprocess.CalledProcessError):
        result["source_commit"] = None
        result["desktop_source_dirty"] = None
    def digest(path):
        with path.open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
    result["files_sha256"] = {
        path.relative_to(bundle).as_posix(): digest(path)
        for path in sorted(bundle.rglob("*"))
        if path.is_file() and not path.is_symlink() and path.name != "build-manifest.json"
    }
    (bundle / "build-manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def stage_notices(bundle):
    """Keep notices current when packaging an already-built onedir bundle."""
    internal = bundle / "_internal"
    shutil.copy2(DESKTOP / "packaging" / "THIRD_PARTY_NOTICES.md", internal / "THIRD_PARTY_NOTICES.md")
    licenses = internal / "licenses"
    shutil.copytree(DESKTOP / "packaging" / "licenses", licenses, dirs_exist_ok=True)
    names = {"pyinstaller", "bleak", "dbus-fast", "pyserial", "shiboken6", "pyside6", "pyside6-addons", "pyside6-essentials", "esptool", "esp-pylib", "cryptography", "cffi", "pycparser", "bitstring", "tibs", "reedsolo", "pyyaml", "intelhex", "rich-click", "click", "rich", "markdown-it-py", "mdurl", "pygments", "websockets", "colorama"}
    for distribution in importlib.metadata.distributions():
        name = distribution.metadata["Name"]
        normalized = name.lower().replace("_", "-")
        if normalized not in names and not normalized.startswith("winrt-"):
            continue
        for file in distribution.files or ():
            if not any(part.endswith(".dist-info") for part in file.parts):
                continue
            if not any(word in file.name.lower() for word in ("license", "licence", "copying", "notice")):
                continue
            source = Path(distribution.locate_file(file))
            if source.is_file():
                target = licenses / "python-distributions" / normalized / file.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    from installer_source import stage_installer_source
    stage_installer_source(bundle)
    # Prefer the actual interpreter's bundled notice when it is available.
    python_candidates = (
        Path(sys.base_prefix) / "LICENSE.txt",
        Path(sys.base_prefix) / "lib" / f"python{sys.version_info.major}.{sys.version_info.minor}" / "LICENSE.txt",
    )
    for source in python_candidates:
        if source.is_file():
            shutil.copy2(source, licenses / "cpython-3.12" / "LICENSE.build-interpreter.txt")
            break


def linux_archive(bundle, output, epoch):
    stem = f"cloud-stick-{version()}-linux-x86_64"
    target = output / f"{stem}.tar.gz"
    with tempfile.TemporaryDirectory(prefix="cloud-stick-stage-", dir=output) as tmp:
        root = Path(tmp) / stem
        root.mkdir()
        shutil.copytree(bundle, root / "cloud-stick", symlinks=True)
        for filename in ("install-user.py", "uninstall-user.py", "cloud-stick.svg", "70-cloud-stick-pro.rules"):
            shutil.copy2(DESKTOP / "packaging" / "linux" / filename, root / filename)
        shutil.copy2(DESKTOP / "packaging" / "linux" / "runtime-packages.txt", root / "runtime-packages.txt")
        shutil.copy2(DESKTOP / "README.md", root / "README.md")
        def normalized(info):
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = epoch
            return info
        with target.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as zipped:
            with tarfile.open(fileobj=zipped, mode="w") as archive:
                archive.add(root, arcname=stem, filter=normalized)
    return target


def linux_deb(bundle, output, epoch):
    if platform.libc_ver() != ("glibc", "2.39"):
        raise SystemExit("The .deb recipe targets the glibc 2.39 build baseline; use --no-deb or rebuild on Ubuntu 24.04")
    if shutil.which("dpkg-deb") is None:
        raise SystemExit("dpkg-deb is required for --deb; use --no-deb for the portable archive")
    target = output / f"cloud-stick_{version()}_amd64.deb"
    with tempfile.TemporaryDirectory(prefix="cloud-stick-deb-", dir=output) as tmp:
        root = Path(tmp)
        shutil.copytree(bundle, root / "opt" / "cloud-stick", symlinks=True)
        (root / "usr" / "bin").mkdir(parents=True)
        (root / "usr" / "bin" / "cloud-stick").symlink_to("/opt/cloud-stick/cloud-stick")
        applications = root / "usr" / "share" / "applications"
        applications.mkdir(parents=True)
        shutil.copy2(DESKTOP / "packaging" / "linux" / "cloud-stick.desktop", applications / "cloud-stick.desktop")
        icons = root / "usr" / "share" / "icons" / "hicolor" / "scalable" / "apps"
        icons.mkdir(parents=True)
        shutil.copy2(DESKTOP / "packaging" / "linux" / "cloud-stick.svg", icons / "cloud-stick.svg")
        control = root / "DEBIAN"
        control.mkdir()
        rules = root / "usr" / "lib" / "udev" / "rules.d"
        rules.mkdir(parents=True)
        shutil.copy2(DESKTOP / "packaging" / "linux" / "70-cloud-stick-pro.rules", rules / "70-cloud-stick-pro.rules")
        (rules / "70-cloud-stick-pro.rules").chmod(0o644)
        shutil.copy2(DESKTOP / "packaging" / "linux" / "postinst", control / "postinst")
        (control / "postinst").chmod(0o755)
        installed_size = sum(path.stat().st_size for path in root.rglob("*") if path.is_file() and not path.is_symlink()) // 1024
        package_lines = (DESKTOP / "packaging" / "linux" / "runtime-packages.txt").read_text(encoding="utf-8").splitlines()
        depends = ", ".join(line.strip() for line in package_lines if line.strip() and not line.lstrip().startswith("#"))
        (control / "control").write_text(
            "Package: cloud-stick\n"
            f"Version: {version()}\nArchitecture: amd64\n"
            "Maintainer: Cloud Stick Setup maintainers\nSection: utils\nPriority: optional\n"
            f"Installed-Size: {installed_size}\n"
            f"Depends: {depends}\n"
            "Recommends: bluez\n"
            "Description: Cloud Stick Setup desktop application\n"
            " Firmware installation, native Bluetooth setup and local diagnostics.\n",
            encoding="utf-8",
        )
        for path in sorted(root.rglob("*"), reverse=True):
            if not path.is_symlink():
                os.utime(path, (epoch, epoch))
        os.utime(root, (epoch, epoch))
        subprocess.run(["dpkg-deb", "--root-owner-group", "--build", "--uniform-compression", "-Zgzip", str(root), str(target)], check=True)
    return target


def windows_archive(bundle, output, epoch):
    target = output / f"cloud-stick-{version()}-windows-x86_64.zip"
    # ZIP timestamps cannot precede 1980. Metadata is stable for fixed inputs.
    import datetime
    timestamp = datetime.datetime.fromtimestamp(max(epoch, 315532800), datetime.timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo("cloud-stick/" + path.relative_to(bundle).as_posix(), date_time=timestamp)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                zipped.writestr(info, path.read_bytes())
    return target


def write_checksums(artifacts, output):
    checksums = []
    for artifact in artifacts:
        with artifact.open("rb") as file:
            digest = hashlib.file_digest(file, "sha256").hexdigest()
        checksums.append(f"{digest}  {artifact.name}\n")
        print(artifact)
    (output / "SHA256SUMS").write_text("".join(checksums), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version=version())
    parser.add_argument("--bundle", type=Path, default=DESKTOP / "dist" / "cloud-stick")
    parser.add_argument("--output", type=Path, default=DESKTOP / "dist" / "releases")
    parser.add_argument("--no-deb", action="store_true", help="Build the Linux portable archive only")
    parser.add_argument("--checksums-only", action="store_true", help="Refresh checksums after optional Windows installer compilation")
    args = parser.parse_args()
    if args.checksums_only:
        artifacts = sorted(args.output.glob(f"cloud-stick-{version()}-windows-x86_64*.zip"))
        artifacts += sorted(args.output.glob(f"cloud-stick-{version()}-windows-x86_64*.exe"))
        if not artifacts:
            parser.error("No Windows release artifacts exist")
        write_checksums(artifacts, args.output)
        return 0
    if sys.platform not in ("linux", "win32") or platform.machine().lower() not in ("x86_64", "amd64"):
        parser.error("This release supports native Linux/Windows x86-64 builds only")
    verify(args.bundle)
    stage_notices(args.bundle)
    args.output.mkdir(parents=True, exist_ok=True)
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    manifest(args.bundle)
    if sys.platform == "linux":
        artifacts = [linux_archive(args.bundle, args.output, epoch)]
        if not args.no_deb:
            artifacts.append(linux_deb(args.bundle, args.output, epoch))
    else:
        artifacts = [windows_archive(args.bundle, args.output, epoch)]
    write_checksums(artifacts, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())

