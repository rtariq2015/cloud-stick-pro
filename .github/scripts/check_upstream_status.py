#!/usr/bin/env python3
"""Notice official upstream changes; never download/install code or contact authors."""

import json
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, Request, build_opener

API = "https://api.github.com/repos/JMcrafter26/cloud-stick/"
BASELINE = "27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d"
LIMIT = 1024 * 1024


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None


def fetch_json(path):
    # Fixed official endpoints only. No token, redirects, configurable URL or cloud account.
    request = Request(API + path, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "cloud-stick-pro-public-maintenance",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    try:
        with build_opener(NoRedirects()).open(request, timeout=15) as response:
            data = response.read(LIMIT + 1)
        if len(data) > LIMIT:
            raise ValueError("response exceeds the monitoring bound")
        return 200, json.loads(data)
    except HTTPError as error:
        if error.code == 404 and path == "license":
            return 404, None
        raise


def inspect(fetch=fetch_json):
    status, commit = fetch("commits/main")
    sha = commit.get("sha") if isinstance(commit, dict) else None
    if status != 200 or not isinstance(sha, str) or not re.fullmatch(r"[a-f0-9]{40}", sha):
        raise ValueError("invalid official upstream commit response")
    license_status, license_record = fetch("license")
    notices = []
    if sha != BASELINE:
        notices.append(f"REVIEW REQUIRED: upstream main changed from {BASELINE} to {sha}.")
    if license_status == 200:
        if not isinstance(license_record, dict) or not license_record.get("sha"):
            raise ValueError("invalid official upstream license response")
        notices.append("REVIEW REQUIRED: upstream now exposes a license file; inspect its grant and retained notices; update the documented licensing evidence.")
    elif license_status != 404:
        raise ValueError("unexpected official upstream license status")
    return notices


def main():
    try:
        notices = inspect()
    except (HTTPError, URLError, TimeoutError, OSError, ValueError) as error:
        print(f"Upstream monitoring: CHECK FAILED ({type(error).__name__}: {error}). No update was applied.", file=sys.stderr)
        return 2
    if notices:
        print("\n".join(notices), file=sys.stderr)
        print("No source, device or release was changed.", file=sys.stderr)
        return 1
    print("Upstream monitoring: unchanged baseline; license file still unavailable. The license-text gap remains documented in docs/CREDITS.md.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
