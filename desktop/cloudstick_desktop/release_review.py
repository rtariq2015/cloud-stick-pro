"""Inspect a local release manifest without executing or installing its contents."""

import json
import re
from pathlib import Path


def review_manifest(path):
    path = Path(path)
    if not path.is_file():
        raise ValueError("invalid_manifest")
    with path.open("rb") as source:
        content = source.read(1024 * 1024 + 1)
    if len(content) > 1024 * 1024:
        raise ValueError("invalid_manifest")
    data = json.loads(content.decode("utf-8"))
    if not isinstance(data, dict):
        raise ValueError("invalid_manifest")
    # Display only bounded, plain identifying fields. A manifest's own assertion
    # of a valid signature is never authentication. No path/URL is followed.
    def label(*keys):
        value = next((data[k] for k in keys if k in data), "Not provided")
        if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9 ._+:/-]{1,120}", value):
            return "Not provided"
        return value

    digest = data.get("sha256", data.get("firmware_sha256", ""))
    if not isinstance(digest, str) or not re.fullmatch(r"[a-fA-F0-9]{64}", digest):
        digest = "Not provided"
    size = data.get("bytes", data.get("size"))
    if type(size) is not int or not 0 < size <= 32 * 1024 * 1024:
        size = None
    return {
        "version": label("version", "firmware_version"),
        "board": label("board", "model"),
        "sha256": digest,
        "bytes": size,
        "signature_status": "Not verified",
        "can_install": False,
        "message": "Manifest inspection only. This release cannot install app or firmware updates. No device or file was changed.",
    }
