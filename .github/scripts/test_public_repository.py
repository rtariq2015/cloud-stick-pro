"""Synthetic publication checks; no device, network or private file access."""

import json
from pathlib import Path
import tempfile
import unittest

from check_public_repository import check


class PublicRepositoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.paths = ["README.md", "docs/guide.md", ".github/public-files.json"]
        (self.root / "docs").mkdir()
        (self.root / ".github").mkdir()
        (self.root / "README.md").write_text("[Guide](docs/guide.md#start)\n")
        (self.root / "docs/guide.md").write_text("[Home](../README.md)\n")
        self.policy()

    def policy(self):
        (self.root / ".github/public-files.json").write_text(json.dumps({
            "schema_version": 1, "publication_stage": "documentation-only",
            "files": self.paths,
        }))

    def test_valid_public_tree(self):
        self.assertEqual(check(self.root, self.paths), [])

    def test_unapproved_source_is_rejected(self):
        self.assertTrue(any("not approved" in e for e in check(self.root, self.paths + ["src/main.cpp"])))

    def test_private_backup_is_rejected(self):
        self.assertTrue(any("not approved" in e for e in check(self.root, self.paths + [".omx/live-nvs.bin"])))

    def test_missing_file_is_rejected(self):
        self.assertTrue(any("missing" in e for e in check(self.root, self.paths[:-1])))

    def test_broken_local_link_is_rejected(self):
        (self.root / "README.md").write_text("[Not published](src/main.cpp)\n")
        self.assertTrue(any("Broken local link" in e for e in check(self.root, self.paths)))

    def test_parent_traversal_is_rejected(self):
        (self.root / "README.md").write_text("[Private](../outside.md)\n")
        self.assertTrue(any("escapes" in e for e in check(self.root, self.paths)))

    def test_urlencoded_traversal_is_rejected(self):
        (self.root / "README.md").write_text("[Private](%2e%2e/outside.md)\n")
        self.assertTrue(any("escapes" in e for e in check(self.root, self.paths)))

    def test_symlink_is_rejected(self):
        (self.root / "docs/guide.md").unlink()
        (self.root / "docs/guide.md").symlink_to(self.root / "README.md")
        self.assertTrue(any("symlink" in e for e in check(self.root, self.paths)))

    def test_key_marker_is_rejected(self):
        (self.root / "README.md").write_text("-----BEGIN " + "PRIVATE KEY-----\n")
        self.assertTrue(any("private key" in e for e in check(self.root, self.paths)))

    def test_github_token_is_rejected(self):
        (self.root / "README.md").write_text("ghp_" + "X" * 36)
        self.assertTrue(any("token" in e for e in check(self.root, self.paths)))

    def test_secure_external_links_are_not_fetched(self):
        (self.root / "README.md").write_text("[Docs](https://example.invalid/docs)\n[Part](#part)\n")
        self.assertEqual(check(self.root, self.paths), [])

    def test_javascript_link_is_rejected(self):
        (self.root / "README.md").write_text("[Bad](javascript:evil)\n")
        self.assertTrue(any("scheme" in e for e in check(self.root, self.paths)))

    def test_duplicate_policy_paths_are_rejected(self):
        self.paths.append("README.md")
        self.policy()
        self.assertTrue(any("schema" in e for e in check(self.root, self.paths)))

    def test_png_header_is_required(self):
        self.paths.append("docs/view.png")
        self.policy()
        (self.root / "docs/view.png").write_bytes(b"unexpected text")
        self.assertTrue(any("not a PNG" in e for e in check(self.root, self.paths)))

    def test_html_image_link_is_checked(self):
        (self.root / "README.md").write_text('<img src="docs/missing.png" width="300">\n')
        self.assertTrue(any("Broken local link" in e for e in check(self.root, self.paths)))

    def test_html_href_is_checked(self):
        (self.root / "README.md").write_text('<a href="../private.md">Private</a>\n')
        self.assertTrue(any("escapes" in e for e in check(self.root, self.paths)))

    def test_valid_html_link(self):
        (self.root / "README.md").write_text('<a href="docs/guide.md">Guide</a>\n')
        self.assertEqual(check(self.root, self.paths), [])


if __name__ == "__main__":
    unittest.main()
