"""Fault injection of the real policy/overlay engine; no USB or firmware runs."""
from dataclasses import replace
import hashlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cloudstick_desktop import firmware_catalog as f, firmware_engine as e
from cloudstick_desktop.firmware_recovery import CHUNK


class MemoryStore:
    def __init__(self, data): self.data = bytes(data)
    def range(self, offset, size): return self.data[offset:offset + size]
    def chunks(self):
        for offset in range(0, len(self.data), CHUNK): yield self.data[offset:offset + CHUNK]


class Device:
    def __init__(self, data): self.data = bytearray(data); self.writes = []; self.proofs = 0; self.fail = self.corrupt = False
    def read(self, offset, size): return bytes(self.data[offset:offset + size])
    def write(self, plan):
        self.writes.append(plan)
        for offset, data in plan: self.data[offset:offset + len(data)] = data
        if self.fail: raise OSError('synthetic disconnect')
        if self.corrupt: self.data[0x9010] ^= 1
    def prove(self): self.proofs += 1


class EngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = b't' * 3072
        cls.old, cls.new = b'o' * 7000, b'n' * 5000
        files = {'bootloader.bin': b'b' * 4096, 'partitions.bin': cls.table, 'nvs-empty.bin': b'\xff' * 0x5000,
                 'otadata-empty.bin': b'\xff' * 0x2000, 'app.bin': cls.new, 'journal.bin': b'j' * 0x100000}
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for name, data in files.items(): z.writestr(name, data)
        cls.raw = stream.getvalue()
        images = tuple(f.Image(name, f.IMAGE_LAYOUT[name][0], len(data), e.sha(data)) for name, data in files.items())
        release = f.Release('1.1.0', 'a' * 40, 'test', 'cloud-stick-firmware-test.cspfw', len(cls.raw), e.sha(cls.raw), images)
        cls.catalog = f.Catalog(1, (release,), (f.KnownApp('1.0.0', len(cls.old), e.sha(cls.old)),
                                               f.KnownApp('1.1.0', len(cls.new), e.sha(cls.new))), (), 'b' * 64)

    def setUp(self):
        self.base = bytearray(b'\xff' * f.FLASH_BYTES)
        self.base[0x8000:0x8c00] = self.table
        self.base[0x10000:0x10000 + len(self.old)] = self.old
        self.base[0x9000:0x900e] = b'private-owner!'
        self.base[0x410100:0x41010e] = b'receipt-data!!'
        self.patcher = patch.object(e, 'PARTITION_SHA256', e.sha(self.table)); self.patcher.start(); self.addCleanup(self.patcher.stop)
        self.events = []

    def run_install(self, device, mode='existing', store=None, resume=False):
        return e.install(device, store or MemoryStore(self.base), self.catalog, self.raw, mode,
                         lambda *args: self.events.append(args[0]), lambda: self.events.append('durable-write-marker'), resume=resume)

    def test_existing_app_only_preserves_every_other_byte_and_sector_tail(self):
        device = Device(self.base)
        self.assertEqual(self.run_install(device), 'installed')
        self.assertEqual(len(device.writes), 1)
        self.assertEqual([offset for offset, _ in device.writes[0]], [0x10000])
        expected = self.base[:]; expected[0x10000:0x10000 + len(self.new)] = self.new
        self.assertEqual(device.data, expected)
        self.assertLess(self.events.index('durable-write-marker'), self.events.index('writing'))
        self.assertGreater(device.proofs, 0)

    def test_full_flash_protected_state_mismatch_blocks_success(self):
        device = Device(self.base); device.corrupt = True
        with self.assertRaisesRegex(f.FirmwareError, 'firmware_readback_failed'): self.run_install(device)
        self.assertNotIn('verified', self.events)

    def test_uncertain_write_never_retried(self):
        device = Device(self.base); device.fail = True
        with self.assertRaises(OSError): self.run_install(device)
        self.assertEqual(len(device.writes), 1)
        self.assertNotIn('verified', self.events)

    def test_owned_firmware_never_factory_installed(self):
        device = Device(self.base)
        with self.assertRaisesRegex(f.FirmwareError, 'firmware_wrong_install_mode'): self.run_install(device, 'new')
        self.assertFalse(device.writes)

    def test_unknown_application_or_table_never_written(self):
        for offset in (0x8001, 0x10001):
            data = self.base[:]; data[offset] ^= 1
            device = Device(data)
            with self.assertRaises(f.FirmwareError): self.run_install(device, store=MemoryStore(data))
            self.assertFalse(device.writes)

    def test_blank_first_install_exact_six_segments(self):
        data = b'\xff' * f.FLASH_BYTES
        device = Device(data)
        self.assertEqual(self.run_install(device, 'new', MemoryStore(data)), 'installed')
        self.assertEqual([offset for offset, _ in device.writes[0]], [0, 0x8000, 0x9000, 0xe000, 0x10000, 0x410000])
        self.assertEqual(device.data[0x510000:], data[0x510000:])

    def test_factory_blocks_owner_namespaces_or_nonempty_journal(self):
        for offset, marker in [(0x9000, b'csp-owner'), (0x9020, b'csp-wifi2'), (0x410000, b'receipt')]:
            data = bytearray(b'\xff' * f.FLASH_BYTES); data[offset:offset + len(marker)] = marker
            device = Device(data)
            with self.assertRaises(f.FirmwareError): self.run_install(device, 'new', MemoryStore(data))
            self.assertFalse(device.writes)

    def test_resume_only_changes_original_write_plan(self):
        partial = self.base[:]; partial[0x10000:0x11000] = b'\xff' * 4096
        device = Device(partial)
        self.assertEqual(self.run_install(device, resume=True), 'installed')
        partial[0x9010] ^= 1
        device = Device(partial)
        with self.assertRaisesRegex(f.FirmwareError, 'firmware_protected_state_changed'): self.run_install(device, resume=True)
        self.assertFalse(device.writes)

    def test_current_image_has_no_write(self):
        data = self.base[:]; data[0x10000:0x10000 + len(self.new)] = self.new
        device = Device(data)
        self.assertEqual(self.run_install(device, store=MemoryStore(data)), 'already_current')
        self.assertFalse(device.writes)

    def test_vendor_automatic_reconnect_and_reflash_disabled(self):
        from types import SimpleNamespace
        esp = SimpleNamespace(WRITE_FLASH_ATTEMPTS=3)
        obj = SimpleNamespace(esp=esp, prove=lambda: None)
        with patch('esptool.cmds.write_flash') as write:
            e.ROMDevice.write(obj, [(0x10000, b'n' * 4096)])
        self.assertEqual(esp.WRITE_FLASH_ATTEMPTS, 1)
        self.assertEqual(write.call_args.kwargs, {'flash_freq': 'keep', 'flash_mode': 'keep', 'flash_size': 'keep', 'compress': False})


class SecurityTests(unittest.TestCase):
    def esp(self):
        from types import SimpleNamespace
        return SimpleNamespace(read_mac=lambda: bytes.fromhex('112233445566'), EFUSE_MAX_KEY=5,
            get_security_info=lambda **_: {'flags': 0, 'api_version': 0, 'chip_id': 9, 'flash_crypt_cnt': 0,
                                          'key_purposes': (0, 0, 0, 0, 0, 0, 12)},
            get_key_block_purpose=lambda _: 0, flash_id=lambda **_: 0x184000, get_psram_cap=lambda: 0)

    def test_security_and_geometry_fail_closed(self):
        self.assertEqual(e.security(self.esp()), '112233445566')
        changes = {'flash_id': lambda **_: 0x164000, 'get_psram_cap': lambda: 2,
                   'get_key_block_purpose': lambda _: 4,
                   'get_security_info': lambda **_: {'flags': 1}}
        for key, value in changes.items():
            esp = self.esp(); setattr(esp, key, value)
            with self.subTest(key=key), self.assertRaises(f.FirmwareError): e.security(esp)
        with self.assertRaisesRegex(f.FirmwareError, 'firmware_device_changed'): e.security(self.esp(), 'aabbccddeeff')


if __name__ == '__main__': unittest.main()
