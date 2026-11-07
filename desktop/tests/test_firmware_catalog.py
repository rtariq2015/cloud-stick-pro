"""Offline tests use a synthetic release key, never a signing secret or hardware."""
import base64
from dataclasses import replace
import hashlib
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cloudstick_desktop import firmware_catalog as f
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.key = Ed25519PrivateKey.generate()
        self.public = self.key.public_key().public_bytes_raw()
        self.payload = json.loads(base64.b64decode(json.loads((Path(__file__).resolve().parents[1] / 'firmware/catalog.json').read_bytes())['payload']))

    def signed(self, payload=None):
        body = json.dumps(payload or self.payload, separators=(',', ':')).encode()
        return json.dumps({'schema': 'cloud-stick-signed-catalog/v1', 'key_id': f.KEY_ID,
                           'payload': base64.b64encode(body).decode(),
                           'signature': base64.b64encode(self.key.sign(body)).decode()}).encode()

    def test_bundled_catalog_has_release_signature(self):
        data = (Path(__file__).resolve().parents[1] / 'firmware/catalog.json').read_bytes()
        c = f.load_catalog(data)
        self.assertEqual(c.latest.version, '0.4.6-cache-dev')
        self.assertEqual(len(c.latest.images), 6)
        self.assertIn('rtariq2015/cloud-stick-pro/releases/download/', c.latest.url)

    def test_signature_corruption_and_untrusted_key_fail(self):
        data = json.loads(self.signed()); data['signature'] = base64.b64encode(bytes(64)).decode()
        for raw, key in [(json.dumps(data).encode(), self.public), (self.signed(), f.PUBLIC_KEY)]:
            with self.assertRaises(f.FirmwareError):
                f.load_catalog(raw, public_key=key)

    def test_trusted_signature_does_not_bypass_layout_or_board_checks(self):
        for path, value in [(('board',), 'other-board'), (('revision',), True),
                            (('releases', 0, 'images', 0, 'offset'), 4096),
                            (('releases', 0, 'asset'), '../app.cspfw'),
                            (('releases', 0, 'source'), 'x' * 40),
                            (('releases', 0, 'bundle_bytes'), f.BUNDLE_LIMIT + 1)]:
            payload = json.loads(json.dumps(self.payload)); node = payload
            for key in path[:-1]: node = node[key]
            node[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(f.FirmwareError):
                f.load_catalog(self.signed(payload), public_key=self.public)

    def test_catalog_rollback_and_duplicate_fields_fail(self):
        with self.assertRaises(f.FirmwareError):
            f.load_catalog(self.signed(), public_key=self.public, minimum_revision=2)
        with self.assertRaises(f.FirmwareError):
            f.strict_json(b'{"a":1,"a":2}')
        with self.assertRaises(f.FirmwareError):
            f.strict_json(b' ' * (f.CATALOG_LIMIT + 1))

    def make_bundle(self, *, extra=False, symlink=False, corrupt=False):
        files = {name: b'x' * (size if name not in ('app.bin', 'bootloader.bin') else 64)
                 for name, (_, size) in f.IMAGE_LAYOUT.items()}
        files['nvs-empty.bin'] = b'\xff' * 0x5000
        files['otadata-empty.bin'] = b'\xff' * 0x2000
        images = tuple(f.Image(name, f.IMAGE_LAYOUT[name][0], len(data), hashlib.sha256(data).hexdigest())
                       for name, data in files.items())
        out = io.BytesIO()
        with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for name, data in files.items():
                info = zipfile.ZipInfo(name); info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o120777 if symlink and name == 'app.bin' else 0o100644) << 16
                z.writestr(info, b'y' * len(data) if corrupt and name == 'app.bin' else data)
            if extra: z.writestr('../escape', b'x')
        raw = out.getvalue()
        return raw, f.Release('1.0.0', 'a' * 40, 'test', 'cloud-stick-firmware-test.cspfw', len(raw), hashlib.sha256(raw).hexdigest(), images)

    def test_bundle_exact_members_hashes_and_sizes(self):
        raw, release = self.make_bundle()
        self.assertEqual(len(f.load_bundle(raw, release)), 6)
        for r in [raw + b'x', raw[:-1]]:
            with self.assertRaises(f.FirmwareError): f.load_bundle(r, release)
        for options in [{'extra': True}, {'symlink': True}, {'corrupt': True}]:
            raw, release = self.make_bundle(**options)
            with self.subTest(options=options), self.assertRaises(f.FirmwareError): f.load_bundle(raw, release)

    def test_network_rejects_unapproved_urls_before_contact(self):
        with patch.object(f.urllib.request, 'build_opener') as opener:
            for url in ['http://github.com/x', 'https://attacker.invalid/a', f.CATALOG_URL + '?x',
                        'https://github.com/other/repo/releases/download/v/cloud-stick-firmware-v.cspfw']:
                with self.assertRaises(f.FirmwareError): f.download(url, 2048)
            opener.assert_not_called()

    def test_redirect_host_scheme_and_credentials_checked(self):
        handler = f.SafeRedirect()
        for url in ['http://github.com/x', 'https://github.com.attacker.invalid/x',
                    'https://user:password@github.com/x', 'https://github.com:444/x']:
            with self.assertRaises(f.FirmwareError):
                handler.redirect_request(None, None, 302, '', {}, url)


if __name__ == '__main__': unittest.main()
