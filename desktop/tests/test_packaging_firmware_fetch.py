# SPDX-License-Identifier: MIT
"""Packaging must validate a selected signed package before replacing its file."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch
from cloudstick_desktop.firmware_catalog import FirmwareError

spec=importlib.util.spec_from_file_location('cloudstick_fetch_firmware',Path(__file__).resolve().parents[1]/'packaging/fetch_firmware.py')
fetcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(fetcher)

class FirmwareFetchTests(unittest.TestCase):
    def fixture(self):
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        root=Path(temporary.name);(root/'catalog.json').write_bytes(b'synthetic signed catalog')
        release=SimpleNamespace(asset='cloud-stick-firmware-1.2.3.cspfw',url='https://synthetic.invalid/release',bundle_size=8)
        return root,release
    def test_verified_cached_package_needs_no_network(self):
        root,release=self.fixture();target=root/release.asset;target.write_bytes(b'verified')
        with patch.object(fetcher,'load_catalog',return_value=SimpleNamespace(latest=release)),patch.object(fetcher,'load_bundle') as verify,patch.object(fetcher,'download') as download:
            self.assertEqual(fetcher.fetch(root),target)
            verify.assert_called_once_with(b'verified',release);download.assert_not_called()
    def test_tampered_download_never_replaces_or_leaves_a_staging_file(self):
        root,release=self.fixture();target=root/release.asset;target.write_bytes(b'old bytes')
        with patch.object(fetcher,'load_catalog',return_value=SimpleNamespace(latest=release)),patch.object(fetcher,'load_bundle',side_effect=FirmwareError('firmware_package_unavailable')),patch.object(fetcher,'download',return_value=b'tampered'):
            with self.assertRaises(FirmwareError):fetcher.fetch(root)
        self.assertEqual(target.read_bytes(),b'old bytes');self.assertEqual(list(root.glob('.verified-firmware-*')),[])
    def test_download_is_verified_and_atomically_installed(self):
        root,release=self.fixture();target=root/release.asset
        with patch.object(fetcher,'load_catalog',return_value=SimpleNamespace(latest=release)),patch.object(fetcher,'load_bundle') as verify,patch.object(fetcher,'download',return_value=b'verified') as download:
            self.assertEqual(fetcher.fetch(root),target)
            download.assert_called_once_with(release.url,8);verify.assert_called_once_with(b'verified',release)
        self.assertEqual(target.read_bytes(),b'verified');self.assertEqual(list(root.glob('.verified-firmware-*')),[])
