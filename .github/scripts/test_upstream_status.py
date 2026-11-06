"""No-network fixtures for the bounded upstream monitor."""

import unittest
from check_upstream_status import BASELINE, inspect


class UpstreamStatusTests(unittest.TestCase):
    def fixture(self, commit=None, license_result=(404, None)):
        return lambda path: (200, {"sha": commit or BASELINE}) if path == "commits/main" else license_result

    def test_unchanged_missing_license(self):
        self.assertEqual(inspect(self.fixture()), [])

    def test_new_upstream_commit_requires_review(self):
        self.assertIn("main changed", inspect(self.fixture("1" * 40))[0])

    def test_new_license_requires_review(self):
        self.assertIn("license file", inspect(self.fixture(license_result=(200, {"sha": "2" * 40})))[0])

    def test_both_changes_are_reported(self):
        self.assertEqual(len(inspect(self.fixture("1" * 40, (200, {"sha": "2" * 40})))), 2)

    def test_invalid_commit_fails(self):
        with self.assertRaises(ValueError):
            inspect(self.fixture("not-a-commit"))

    def test_invalid_license_fails(self):
        with self.assertRaises(ValueError):
            inspect(self.fixture(license_result=(200, {})))

    def test_network_error_is_not_success(self):
        def timeout(path):
            raise TimeoutError("synthetic timeout")
        with self.assertRaises(TimeoutError):
            inspect(timeout)

    def test_redirect_status_is_not_success(self):
        with self.assertRaises(ValueError):
            inspect(self.fixture(license_result=(302, None)))


if __name__ == "__main__":
    unittest.main()
