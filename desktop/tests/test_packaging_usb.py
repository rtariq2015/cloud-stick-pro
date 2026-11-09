"""Inspect synthetic packages and mock maintenance commands; no OS/device changes."""

import importlib.util
from pathlib import Path
import stat
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

PACKAGING = Path(__file__).resolve().parents[1] / "packaging"
sys.path.insert(0, str(PACKAGING))
spec = importlib.util.spec_from_file_location("cloudstick_package_artifacts_usb", PACKAGING / "package_artifacts.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackagingUSBTests(unittest.TestCase):
    def test_rule_is_device_specific_active_session_access(self):
        lines = (PACKAGING / "linux/70-cloud-stick-pro.rules").read_text().splitlines()
        effective = [line for line in lines if line and not line.startswith("#")]
        self.assertEqual(effective, ['ACTION!="remove", SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", TAG+="uaccess"'])
        self.assertNotIn("MODE=", "\n".join(effective))
        self.assertNotIn("GROUP=", "\n".join(effective))

    def test_deb_stages_exact_rule_and_executable_targeted_postinst(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle, output = root / "bundle", root / "output"
            bundle.mkdir(); output.mkdir()
            (bundle / "cloud-stick").write_bytes(b"synthetic application")
            inspected = []
            def fake_dpkg(command, **options):
                self.assertEqual(command[:2], ["dpkg-deb", "--root-owner-group"])
                staging = Path(command[-2])
                rule = staging / "usr/lib/udev/rules.d/70-cloud-stick-pro.rules"
                postinst = staging / "DEBIAN/postinst"
                self.assertEqual(rule.read_bytes(), (PACKAGING / "linux/70-cloud-stick-pro.rules").read_bytes())
                self.assertEqual(stat.S_IMODE(rule.stat().st_mode), 0o644)
                self.assertEqual(postinst.read_bytes(), (PACKAGING / "linux/postinst").read_bytes())
                self.assertEqual(stat.S_IMODE(postinst.stat().st_mode), 0o755)
                self.assertIn("Name=Cloud Stick Setup\n", (staging / "usr/share/applications/cloud-stick.desktop").read_text())
                self.assertIn("Version: 0.7.4\n", (staging / "DEBIAN/control").read_text())
                self.assertIn("Description: Cloud Stick Setup desktop application\n", (staging / "DEBIAN/control").read_text())
                inspected.append(True)
            with patch.object(package.platform, "libc_ver", return_value=("glibc", "2.39")), \
                    patch.object(package.shutil, "which", return_value="/usr/bin/dpkg-deb"), \
                    patch.object(package.subprocess, "run", side_effect=fake_dpkg):
                package.linux_deb(bundle, output, 0)
            self.assertEqual(inspected, [True])

    def test_portable_contains_same_rule_without_automatic_system_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle, output = root / "bundle", root / "output"
            bundle.mkdir(); output.mkdir()
            (bundle / "cloud-stick").write_bytes(b"synthetic application")
            artifact = package.linux_archive(bundle, output, 0)
            with tarfile.open(artifact) as archive:
                rules = [entry for entry in archive.getmembers() if entry.name.endswith("/70-cloud-stick-pro.rules")]
                self.assertEqual(len(rules), 1)
                self.assertEqual(archive.extractfile(rules[0]).read(), (PACKAGING / "linux/70-cloud-stick-pro.rules").read_bytes())
                self.assertFalse(any(entry.name.endswith("/postinst") for entry in archive.getmembers()))
            installer = (PACKAGING / "linux/install-user.py").read_text()
            self.assertNotIn("udevadm", installer)

    def test_postinst_only_reloads_rules_and_triggers_acm_change(self):
        # The maintained script is intentionally tiny: exact command review
        # avoids executing even a mocked privileged maintenance entrypoint.
        script = (PACKAGING / "linux/postinst").read_text()
        effective = [line.strip() for line in script.splitlines()
                     if line.strip() and not line.startswith("#")]
        self.assertEqual(effective, ["set -e",
            'if [ "${1:-}" = configure ] && command -v udevadm >/dev/null 2>&1; then',
            "udevadm control --reload-rules || true",
            "udevadm trigger --action=change --subsystem-match=tty --sysname-match='ttyACM*' || true",
            "fi", "exit 0"])


if __name__ == "__main__":
    unittest.main()
