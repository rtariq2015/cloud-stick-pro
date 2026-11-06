#!/usr/bin/env python3
"""Check the exact reviewed public tree without network or secrets.

This is a publication allowlist, not a complete credential scanner or a firmware
test. A maintainer must review policy changes before admitting additional files.
"""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

POLICY = ".github/public-files.json"
LINK = re.compile(r"!?\[[^\]\n]*\]\(([^\s)]+)(?:\s+[^)]*)?\)")
PRIVATE_MATERIAL = re.compile(
    r"-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----|"
    r"\bgh[pousr]_[A-Za-z0-9]{30,}\b|"
    r"\bgithub_pat_[A-Za-z0-9_]{50,}\b"
)


class HTMLLinks(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.targets = []

    def handle_starttag(self, tag, attributes):
        self.targets.extend(value for key, value in attributes
                            if key in ("src", "href") and value)


def link_targets(text):
    html = HTMLLinks()
    html.feed(text)
    return [match.group(1).strip("<>") for match in LINK.finditer(text)] + html.targets


def check(root: Path, tracked: list[str]) -> list[str]:
    errors: list[str] = []
    try:
        policy = json.loads((root / POLICY).read_text(encoding="utf-8"))
        approved = policy["files"]
        if (policy["schema_version"] != 1
                or policy["publication_stage"] not in ("documentation-only", "source-preview")
                or not isinstance(approved, list)
                or not all(isinstance(path, str) for path in approved)
                or len(set(approved)) != len(approved)):
            raise ValueError("unexpected policy schema or stage")
        links = policy.get("link_check_files")
        if links is not None and (
                not isinstance(links, list) or not links
                or not all(isinstance(path, str) for path in links)
                or len(set(links)) != len(links)
                or any(path not in approved or not path.endswith(".md")
                       or PurePosixPath(path).is_absolute()
                       or ".." in PurePosixPath(path).parts or "\\" in path
                       for path in links)):
            raise ValueError("invalid curated documentation list")
    except (OSError, ValueError, KeyError, TypeError) as error:
        return [f"Invalid publication policy: {error}"]

    paths = set(tracked)
    allowed = set(approved)
    for path in sorted(paths - allowed):
        errors.append(f"File is not approved for publication: {path}")
    for path in sorted(allowed - paths):
        errors.append(f"Approved file is missing: {path}")

    for name in sorted(paths & allowed):
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or "\\" in name:
            errors.append(f"Unsafe repository path: {name}")
            continue
        file = root / name
        if file.is_symlink() or not file.is_file() or not file.resolve().is_relative_to(root.resolve()):
            errors.append(f"File is missing, a symlink, or outside the root: {name}")
            continue
        try:
            data = file.read_bytes()
        except OSError:
            errors.append(f"Cannot read approved file: {name}")
            continue
        # ASCII key/token sentinels remain detectable in explicitly reviewed
        # binary assets. This is still not a comprehensive credential scanner.
        if PRIVATE_MATERIAL.search(data.decode("utf-8", errors="replace")):
            errors.append(f"Possible private key/token material: {name}")
        if file.suffix == ".png":
            if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                errors.append(f"Image is not a PNG: {name}")
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeError:
            if policy["publication_stage"] != "source-preview":
                errors.append(f"Expected readable UTF-8 text: {name}")
            continue
        if file.suffix != ".md" or (links is not None and name not in links):
            continue
        for target in link_targets(text):
            parsed = urlsplit(target)
            if parsed.scheme:
                if parsed.scheme not in ("https", "mailto"):
                    errors.append(f"Unsupported link scheme in {name}: {parsed.scheme}")
                continue
            if parsed.netloc:
                errors.append(f"Protocol-relative link in {name}: {target}")
                continue
            if not parsed.path:
                continue  # An in-page fragment does not name another file.
            decoded = unquote(parsed.path)
            destination = (file.parent / decoded).resolve()
            if decoded.startswith("/") or not destination.is_relative_to(root.resolve()):
                errors.append(f"Link escapes the repository in {name}: {target}")
                continue
            dest_name = destination.relative_to(root.resolve()).as_posix()
            if dest_name not in paths or not destination.is_file():
                errors.append(f"Broken local link in {name}: {target}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True, stdout=subprocess.PIPE,
    )
    paths = result.stdout.decode("utf-8").rstrip("\0").split("\0") if result.stdout else []
    errors = check(root, paths)
    if errors:
        print("Public repository checks: FAIL", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(f"Public repository checks: PASS ({len(paths)} approved files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
