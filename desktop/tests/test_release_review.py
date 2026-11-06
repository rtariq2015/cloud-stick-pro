import json
from pathlib import Path
import tempfile
import unittest

from cloudstick_desktop.release_review import review_manifest


class ReleaseReviewTests(unittest.TestCase):
    def review(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "release.json"
            path.write_text(json.dumps(data))
            return review_manifest(path)

    def test_signature_claim_does_not_authorize_installation(self):
        result = self.review({"version": "0.4.0-rc5", "board": "T-Dongle-S3", "bytes": 1680976,
                              "sha256": "ab" * 32, "signature_verified": True})
        self.assertEqual(result["version"], "0.4.0-rc5")
        self.assertEqual(result["sha256"], "ab" * 32)
        self.assertFalse(result["can_install"])
        self.assertEqual(result["signature_status"], "Not verified")

    def test_credentials_and_executable_content_are_not_returned(self):
        secret = "SYNTHETIC-DO-NOT-DISPLAY"
        result = self.review({"password": secret, "owner_key": secret,
                              "url": "https://example.invalid/", "execute": secret})
        self.assertNotIn(secret, json.dumps(result))
        self.assertNotIn("url", result)
        self.assertFalse(result["can_install"])

    def test_invalid_scalar_fields_are_rejected(self):
        result = self.review({"version": "<script>alert(1)</script>", "board": "a\nb",
                              "sha256": "x" * 64, "bytes": True})
        self.assertEqual(result["version"], "Not provided")
        self.assertEqual(result["board"], "Not provided")
        self.assertEqual(result["sha256"], "Not provided")
        self.assertIsNone(result["bytes"])

    def test_nested_release_cannot_be_mistaken_for_reviewed_image(self):
        result = self.review({"version": {"value": "0.4.0"}, "bytes": 1 << 40})
        self.assertEqual(result["version"], "Not provided")
        self.assertIsNone(result["bytes"])

    def test_non_object(self):
        with self.assertRaises(ValueError):
            self.review(["not a manifest"])

    def test_bound_before_parse(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "huge.json"
            path.write_bytes(b" " * (1024 * 1024 + 1))
            with self.assertRaises(ValueError):
                review_manifest(path)

    def test_missing_file(self):
        with self.assertRaises(ValueError):
            review_manifest("/this-file-does-not-exist/release.json")


if __name__ == "__main__":
    unittest.main()
