"""Exercise the actual exporter against isolated synthetic Git repositories."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("public_export", SOURCE / "tools/export_public_project.py")
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="csp-public-export-")
        self.base = Path(self.temp.name)
        self.repo = self.base / "source"
        self.repo.mkdir()
        self.previous = exporter.ROOT
        exporter.ROOT = self.repo
        self.git("init", "-q")
        self.git("config", "user.name", "Synthetic fixture")
        self.git("config", "user.email", "fixture@example.invalid")

    def tearDown(self):
        exporter.ROOT = self.previous
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.repo, stderr=subprocess.STDOUT)

    def write(self, name, content):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-qm", "Synthetic release")

    def test_committed_blobs_only_and_private_names_excluded(self):
        self.write("README.md", "Reviewed source\n")
        self.write("src/main.cpp", "int main() { return 0; }\n")
        self.write("artifacts/live-backup.bin", "synthetic private bytes")
        self.write("owner-card.json", '{"fixture":"not for export"}')
        self.write(".omx/context/local.json", "synthetic private context")
        self.write("android/local.properties", "sdk.dir=/private/fixture")
        self.commit()
        self.write("src/main.cpp", "uncommitted edit must not be exported")
        self.write("src/untracked.cpp", "untracked data must not be exported")
        output = self.base / "release"
        result = exporter.export(output)
        self.assertEqual({x["path"] for x in result["files"]}, {"README.md", "src/main.cpp"})
        self.assertIn("return 0", (output / "src/main.cpp").read_text())
        self.assertFalse((output / ".git").exists())

    def test_curated_public_history_links_remain_repository_relative(self):
        self.write("README.md", "[History](docs/PROJECT_HISTORY.md#ordered-milestones)\n")
        self.write("docs/PROJECT_HISTORY.md", "[Readme](../README.md) [guide](GETTING_STARTED.md)\n")
        self.write("docs/GETTING_STARTED.md", "[history](PROJECT_HISTORY.md)\n")
        self.commit()
        output = self.base / "release"
        exporter.export(output)
        self.assertIn("docs/PROJECT_HISTORY.md#ordered-milestones", (output / "README.md").read_text())
        self.assertIn("../README.md", (output / "docs/PROJECT_HISTORY.md").read_text())
        self.assertIn("GETTING_STARTED.md", (output / "docs/PROJECT_HISTORY.md").read_text())
        self.assertIn("PROJECT_HISTORY.md", (output / "docs/GETTING_STARTED.md").read_text())

    def test_operator_docs_and_raw_history_cannot_hitchhike(self):
        self.write("README.md", "Public overview\n")
        self.write("docs/PROJECT_HISTORY.md", "Curated public milestones\n")
        # Public synthetic sentinels: never use real paths, hosts or board IDs.
        private_text = "SYNTHETIC_OPERATOR_ONLY /private/fixture https://owner.example.invalid/ CSP-W2-a1b2c3"
        excluded = ["CURRENT_WORK_HANDOFF.md", "AUTONOMOUS_COMPUTER_BENCH.md",
                    "MILESTONE2_OWNER_CARD.md", "MILESTONE2_INSTALLATION_RESULTS.md",
                    "PROJECT_PROGRESS_AND_RELEASE_INDEX.md", "PROJECT_REVISION_HISTORY.json",
                    "AGENTS.md", "NEW_UNREVIEWED_REPORT.md",
                    "personal.code-workspace", "docs/private-workspace.md",
                    "docs/history/README_0.3.3.md", "docs/history/recovery.md",
                    "docs/unreviewed-report.md", "docs/images/private-screen.png",
                    "src/developer-debug.elf", "src/developer-debug.map"]
        for name in excluded:
            self.write(name, private_text)
        self.commit()
        output, archive = self.base / "release", self.base / "release.zip"
        manifest = exporter.export(output, archive=archive)
        self.assertEqual({item["path"] for item in manifest["files"]},
                         {"README.md", "docs/PROJECT_HISTORY.md"})
        self.assertTrue(manifest["source_content_requires_review"])
        for path in output.rglob("*"):
            if path.is_file():
                self.assertNotIn(private_text.encode(), path.read_bytes())
                self.assertNotIn(b"SYNTHETIC_OPERATOR_ONLY", path.read_bytes())
        with zipfile.ZipFile(archive) as packaged:
            for name in packaged.namelist():
                self.assertNotIn(b"SYNTHETIC_OPERATOR_ONLY", packaged.read(name))

    def test_curated_guides_component_licenses_and_named_previews_are_retained(self):
        allowed = ["README.md", "README.upstream.md", "UPSTREAM_NOTICE.md", "CHANGELOG.md",
                   "CONTRIBUTING.md", "SECURITY.md", "platformio.ini", "partitions.csv",
                   "sdkconfig.defaults", "cloud-stick-pro.code-workspace",
                   "docs/CANONICAL_PROJECT.md", "docs/RELEASE_NOTES.md", "docs/DEVELOPER_GUIDE.md", "docs/CREDITS.md",
                   "docs/UPDATING.md", "docs/MAINTENANCE.md",
                   "docs/releases/0.4.2-source-review.json", "docs/images/lcd-states.png",
                   "desktop/packaging/licenses/fixture/LICENSE.txt",
                   "android/licenses/Apache-2.0.txt", "sdk/pinned_ble/LICENSE"]
        for name in allowed:
            self.write(name, "Public synthetic content\n")
        self.commit()
        manifest = exporter.export(self.base / "release")
        self.assertEqual({item["path"] for item in manifest["files"]}, set(allowed))

    def test_reusable_secret_marker_aborts_before_output(self):
        self.write("src/accidental.txt", "ghp_" + "A" * 36)
        self.commit()
        output = self.base / "release"
        with self.assertRaisesRegex(ValueError, "private material marker"):
            exporter.export(output)
        self.assertFalse(output.exists())

    def test_symlink_does_not_copy_outside_content(self):
        self.write("README.md", "fixture\n")
        (self.repo / "src").mkdir()
        (self.base / "private.txt").write_text("synthetic outside content")
        (self.repo / "src/alias").symlink_to(self.base / "private.txt")
        self.commit()
        with self.assertRaisesRegex(ValueError, "unsupported source entry"):
            exporter.export(self.base / "release")

    def test_existing_output_is_preserved(self):
        self.write("README.md", "fixture\n")
        self.commit()
        output = self.base / "release"
        output.mkdir()
        (output / "keep.txt").write_text("keep me")
        with self.assertRaisesRegex(ValueError, "already exists"):
            exporter.export(output)
        self.assertEqual((output / "keep.txt").read_text(), "keep me")

    def test_hashes_executable_mode_and_archive(self):
        path = self.write("tools/check.sh", "#!/bin/sh\nexit 0\n")
        path.chmod(0o755)
        self.commit()
        output, archive = self.base / "release", self.base / "release.zip"
        result = exporter.export(output, archive=archive)
        item = result["files"][0]
        self.assertEqual(item["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual((output / "tools/check.sh").stat().st_mode & 0o777, 0o755)
        with zipfile.ZipFile(archive) as packaged:
            self.assertEqual(packaged.read("release/tools/check.sh"), path.read_bytes())
            self.assertFalse(any(".git/" in name for name in packaged.namelist()))
        self.assertEqual(json.loads((output / "SOURCE_MANIFEST.json").read_text()), result)


if __name__ == "__main__":
    unittest.main(verbosity=2)
