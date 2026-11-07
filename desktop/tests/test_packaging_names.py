"""Version/menu and temporary-directory installer tests; no real app install."""

import ast
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

DESKTOP = Path(__file__).resolve().parents[1]
PACKAGING = DESKTOP / "packaging"
sys.path.insert(0, str(PACKAGING))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load("cloudstick_package_names", PACKAGING / "package_artifacts.py")
installer = load("cloudstick_user_installer_names", PACKAGING / "linux/install-user.py")


class PackagingNamesTests(unittest.TestCase):
    def test_release_version_sources_agree(self):
        metadata = tomllib.loads((DESKTOP / "pyproject.toml").read_text())
        tree = ast.parse((DESKTOP / "cloudstick_desktop/__init__.py").read_text())
        runtime_version = next(ast.literal_eval(node.value) for node in tree.body
                               if isinstance(node, ast.Assign)
                               and any(isinstance(target, ast.Name) and target.id == "__version__" for target in node.targets))
        self.assertEqual(metadata["project"]["version"], "0.5.5")
        self.assertEqual(runtime_version, "0.5.5")
        self.assertEqual(package.version(), "0.5.5")
        self.assertIn('#define AppVersion "0.5.5"', (PACKAGING / "windows/cloud-stick.iss").read_text())

    def test_linux_and_windows_menu_labels_preserve_application_paths(self):
        entry = (PACKAGING / "linux/cloud-stick.desktop").read_text().splitlines()
        self.assertIn("Name=Cloud Stick Setup", entry)
        self.assertIn("StartupWMClass=Cloud Stick Setup", entry)
        self.assertIn("Exec=/opt/cloud-stick/cloud-stick", entry)
        windows = (PACKAGING / "windows/cloud-stick.iss").read_text()
        self.assertIn("AppName=Cloud Stick Setup\n", windows)
        self.assertIn("DefaultGroupName=Cloud Stick Setup\n", windows)
        self.assertIn('Name: "{userprograms}\\Cloud Stick Setup";', windows)
        self.assertIn("DefaultDirName={localappdata}\\Programs\\Cloud Stick\n", windows)

    def test_build_manifest_has_current_name_version_and_file_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary)
            (bundle / "cloud-stick").write_bytes(b"synthetic")
            with patch.object(package.platform, "platform", return_value="synthetic-linux"), \
                    patch.object(package.subprocess, "check_output", side_effect=["a" * 40 + "\n", ""]):
                package.manifest(bundle)
            record = json.loads((bundle / "build-manifest.json").read_text())
            self.assertEqual(record["application"], "Cloud Stick Setup")
            self.assertEqual(record["version"], "0.5.5")
            self.assertEqual(set(record["files_sha256"]), {"cloud-stick"})

    def run_synthetic_installer(self, root):
        release = root / "release"
        home = root / "home"
        data = home / "data"
        (release / "cloud-stick").mkdir(parents=True, exist_ok=True)
        (release / "cloud-stick/cloud-stick").write_bytes(b"synthetic-application")
        (release / "uninstall-user.py").write_text("# synthetic uninstaller\n")
        (release / "cloud-stick.svg").write_text("<svg/>\n")
        return contextlib.ExitStack(), release, home, data

    def test_user_install_has_new_label_and_refuses_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            stack, release, home, data = self.run_synthetic_installer(Path(temporary))
            with stack, patch.object(installer, "__file__", str(release / "install-user.py")), \
                    patch.object(installer.Path, "home", return_value=home), \
                    patch.object(installer.os, "geteuid", return_value=1000), \
                    patch.dict(installer.os.environ, {"XDG_DATA_HOME": str(data)}), \
                    patch.object(sys, "argv", ["install-user.py"]), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(installer.main(), 0)
                prefix = home / ".local/opt/cloud-stick-desktop"
                menu = data / "applications/cloud-stick-desktop.desktop"
                self.assertIn("Name=Cloud Stick Setup\n", menu.read_text())
                self.assertIn("StartupWMClass=Cloud Stick Setup\n", menu.read_text())
                self.assertEqual((prefix / ".cloud-stick-user-install").read_text(), "cloud-stick-desktop\n")
                original = (prefix / "cloud-stick").read_bytes()
                with self.assertRaises(SystemExit) as caught:
                    installer.main()
                self.assertEqual(caught.exception.code, 2)
                self.assertEqual((prefix / "cloud-stick").read_bytes(), original)

    def test_user_install_refuses_unrelated_launcher_collision(self):
        with tempfile.TemporaryDirectory() as temporary:
            stack, release, home, data = self.run_synthetic_installer(Path(temporary))
            launcher = home / ".local/bin/cloud-stick"
            launcher.parent.mkdir(parents=True)
            launcher.write_text("unrelated launcher\n")
            with stack, patch.object(installer, "__file__", str(release / "install-user.py")), \
                    patch.object(installer.Path, "home", return_value=home), \
                    patch.object(installer.os, "geteuid", return_value=1000), \
                    patch.dict(installer.os.environ, {"XDG_DATA_HOME": str(data)}), \
                    patch.object(sys, "argv", ["install-user.py"]), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    installer.main()
            self.assertEqual(launcher.read_text(), "unrelated launcher\n")
            self.assertFalse((home / ".local/opt/cloud-stick-desktop").exists())


if __name__ == "__main__":
    unittest.main()
