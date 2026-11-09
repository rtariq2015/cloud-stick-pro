#!/usr/bin/env python3
"""Export a clean, organized source snapshot, without Git history or local data.

This deliberately reads Git blobs instead of copying the working directory.
Ignored vaults, build products, local settings and accidental untracked files
cannot hitch a ride. A fresh output directory is required; nothing is deleted.
The result is a review package, not permission to redistribute upstream code.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = frozenset(("src", "include", "lib", "boards", "sdk", "data", "test",
                      "tools", "phone", "desktop", "android", ".github"))
TOP_LEVEL = frozenset(("README.md", "README.upstream.md", "UPSTREAM_NOTICE.md",
                       "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md",
                       ".gitignore", "platformio.ini", "partitions.csv",
                       "sdkconfig.defaults", "cloud-stick-pro.code-workspace"))
# Public guides are reviewed individually. Raw operator, recovery, enrollment
# and historical dossiers stay in the working repository, never the export.
PUBLIC_DOCS = frozenset((
    "docs/COMPANION_APPS.md", "docs/images/desktop-companion.png", "docs/releases/2026-11-08-guided-companion.md",
    "docs/licenses/companion-updates-MIT.txt",
    "docs/releases/2026-11-08-android-emulator.md",
    "docs/releases/2026-11-08-android-dashboard.md", "docs/images/android-dashboard.png",
    "docs/releases/2026-11-07-integrated-setup.md",
    "docs/README.md", "docs/TROUBLESHOOTING.md", "docs/REPOSITORY_REVIEW.md",
    "docs/provenance/README.md", "docs/provenance/2026-11-06-source-export.json",
    "docs/images/cloud-stick-overview.svg",
    "docs/INTEGRATED_SETUP.md", "docs/images/desktop-firmware.png",
    "docs/CREDITS.md", "docs/DEVELOPER_GUIDE.md", "docs/GETTING_STARTED.md",
    "docs/PROJECT_LAYOUT.md", "docs/PROJECT_HISTORY.md", "docs/RELEASES.md",
    "docs/STORAGE_SAFETY.md", "docs/REFERENCE_REVIEW.md",
    "docs/CANONICAL_PROJECT.md",
    "docs/RELEASE_NOTES.md",
    "docs/UPDATING.md", "docs/MAINTENANCE.md",
    "docs/OWNER_LOGIN_FIRMWARE.md", "docs/OWNER_CLOUD_FIRMWARE_IMPLEMENTATION.md",
    "docs/releases/0.4.2-source-review.json",
    "docs/releases/2026-11-06-improvements.md",
    "docs/releases/2026-11-06-wifi-fallback.md",
    "docs/releases/2026-11-06-ble-cleanup.md",
    "docs/images/desktop-setup.png", "docs/images/desktop-troubleshoot.png",
    "docs/images/android-viewer.png",
    "docs/images/lcd-states.png",
))
SKIP_PARTS = frozenset(("artifacts", "build", "dist", ".gradle", ".venv",
                        "__pycache__", ".omx", ".codex", ".pio", ".openai"))
PRIVATE_NAMES = frozenset(("owner-card.json", "owner-record.bin", "bluetoothkey.txt",
                           "setup-access-v1.json", "original-internal-flash.bin",
                           "local.properties", ".env", "AGENTS.md",
                           "PROJECT_REVISION_HISTORY.json"))
# Debug ELF/map products stay developer-local; do not rewrite pinned binaries.
PRIVATE_SUFFIXES = (".keystore", ".jks", ".p12", ".pfx", ".enc", ".bundle",
                    ".elf", ".map", ".csprec", ".key")
SECRETS = re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|"
                     rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b|"
                     rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b")
LINK = re.compile(r"(!?\[[^\]]*\]\()([^\s)]+)(\))")


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", *args], cwd=ROOT)


def destination(name: str) -> str | None:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("unsafe source path")
    if path.name in PRIVATE_NAMES or path.name.lower().endswith(PRIVATE_SUFFIXES):
        return None
    if any(part in SKIP_PARTS for part in path.parts):
        return None
    if name == ".vscode/settings.json":
        return name
    if path.parts[0] == "docs":
        return name if name in PUBLIC_DOCS else None
    if len(path.parts) == 1:
        return name if name in TOP_LEVEL else None
    return name if path.parts[0] in PREFIXES else None


def rewrite_links(data: bytes, source: str, target: str,
                  mapping: dict[str, str]) -> bytes:
    if not source.endswith(".md"):
        return data
    text = data.decode("utf-8")

    def replace(match: re.Match[str]) -> str:
        link = match[2]
        if re.match(r"[a-zA-Z][a-zA-Z0-9+.-]*:", link) or link.startswith(("#", "/")):
            return match[0]
        bare, sep, anchor = link.partition("#")
        resolved = os.path.normpath(str(PurePosixPath(source).parent / bare))
        if resolved not in mapping:
            return match[0]
        relative = os.path.relpath(mapping[resolved], str(PurePosixPath(target).parent))
        return match[1] + relative + (sep + anchor if sep else "") + match[3]

    return LINK.sub(replace, text).encode("utf-8")


def export(output: Path, revision: str = "HEAD", archive: Path | None = None) -> dict:
    output = output.resolve()
    if output == ROOT or output.is_relative_to(ROOT) or ROOT.is_relative_to(output):
        raise ValueError("output must be a new directory outside the source tree")
    if output.exists():
        raise ValueError("output already exists; preserved unchanged")
    commit = git("rev-parse", "--verify", revision + "^{commit}").decode().strip()
    entries = []
    for line in git("ls-tree", "-rz", "--full-tree", commit).split(b"\0"):
        if not line:
            continue
        metadata, raw_name = line.split(b"\t", 1)
        mode, kind, blob = metadata.decode().split()
        name = raw_name.decode("utf-8")
        target = destination(name)
        if target is None:
            continue
        if mode not in ("100644", "100755") or kind != "blob":
            raise ValueError("unsupported source entry: " + name)
        entries.append((name, target, mode, blob))
    mapping = {source: target for source, target, _, _ in entries}
    if len(set(mapping.values())) != len(mapping):
        raise ValueError("two source files map to one destination")
    files = []
    # Validate every blob before creating the export. Error messages never
    # include matched credential contents.
    for source, target, mode, blob in entries:
        data = git("cat-file", "blob", blob)
        if SECRETS.search(data):
            raise ValueError("private material marker in tracked file: " + source)
        data = rewrite_links(data, source, target, mapping)
        files.append((target, mode, data, source, blob))
    output.mkdir(mode=0o755, parents=True)
    manifest = {"schema": "cloud-stick-public-source/v1", "source_commit": commit,
                "history_included": False, "private_artifacts_included": False,
                "documentation_policy": "Explicit public-guide allowlist; raw operator/recovery/history dossiers omitted",
                "source_content_requires_review": True,
                "license_status": "Upstream README asserts MIT; exact license file is missing. See retained evidence in CREDITS.",
                "files": []}
    for target, mode, data, source, blob in files:
        path = output / target
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(0o755 if mode == "100755" else 0o644)
        manifest["files"].append({"path": target, "source_path": source,
                                   "git_blob": blob, "bytes": len(data),
                                   "sha256": hashlib.sha256(data).hexdigest()})
    (output / "SOURCE_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if archive is not None:
        if archive.exists():
            raise ValueError("archive already exists; preserved unchanged")
        archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
            for path in sorted(output.rglob("*")):
                if not path.is_file():
                    continue
                item = zipfile.ZipInfo(output.name + "/" + path.relative_to(output).as_posix(),
                                       date_time=(2026, 11, 6, 0, 0, 0))
                item.compress_type = zipfile.ZIP_DEFLATED
                item.external_attr = (path.stat().st_mode & 0o777) << 16
                zipped.writestr(item, path.read_bytes())
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    manifest = export(args.output, args.revision, args.archive)
    print(json.dumps({"source_commit": manifest["source_commit"],
                      "exported_files": len(manifest["files"]),
                      "output": str(args.output), "published": False}))


if __name__ == "__main__":
    main()
