"""Synthetic flash bytes only. Real native Windows crypto/ACL tests run there."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cloudstick_desktop import firmware_recovery as r
from cloudstick_desktop.firmware_catalog import FirmwareError


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'private'
        self.metadata = {'board_mac': 'synthetic-identity', 'target': 'synthetic-target'}
        self.gate = patch.object(r, 'require_no_swap'); self.gate.start(); self.addCleanup(self.gate.stop)
        self.geometry = patch.object(r, 'FLASH_BYTES', 2 * r.CHUNK); self.geometry.start(); self.addCleanup(self.geometry.stop)
        self.data = b'SYNTHETIC-PRIVATE-FLASH-' * 6000
        self.data = self.data[:2 * r.CHUNK]

    def capture(self):
        store = r.RecoveryStore(self.root, self.metadata)
        store.capture(lambda start, size: self.data[start:start + size], lambda *_: None)
        return store

    def test_two_fresh_reads_reopened_and_exactly_compared(self):
        calls = []
        store = r.RecoveryStore(self.root, self.metadata)
        digest = store.capture(lambda start, size: (calls.append(start), self.data[start:start + size])[1], lambda *_: None)
        self.assertEqual(calls, [0, r.CHUNK, 0, r.CHUNK])
        self.assertEqual(b''.join(store.chunks()), self.data)
        self.assertEqual(b''.join(store.chunks('b')), self.data)
        self.assertEqual(store.range(500, 70000), self.data[500:70500])
        self.assertNotIn(self.data[:100], (self.root / 'a.csprec').read_bytes())
        if os.name == 'posix': self.assertEqual((self.root / 'recovery.key').stat().st_mode & 0o777, 0o600)
        key = store.key; store.close(); self.assertEqual(key, bytes(32))

    def test_mismatched_second_read_refuses_checkpoint(self):
        store = r.RecoveryStore(self.root, self.metadata); count = 0
        def read(start, size):
            nonlocal count
            count += 1
            data = self.data[start:start + size]
            return data if count < 3 else b'x' * size
        with self.assertRaisesRegex(FirmwareError, 'recovery_reads_differ'): store.capture(read, lambda *_: None)

    def test_truncation_corruption_and_reordered_chunks_fail(self):
        store = self.capture(); path = self.root / 'a.csprec'; raw = path.read_bytes()
        for altered in (raw[:-1], raw + b'x', raw[:100] + bytes([raw[100] ^ 1]) + raw[101:]):
            path.write_bytes(altered)
            with self.assertRaises(FirmwareError): list(store.chunks())
        path.write_bytes(raw)
        different = r.RecoveryStore(self.root, {'board_mac': 'wrong'}, create=False)
        with self.assertRaises(FirmwareError): list(different.chunks())
        store.close(); different.close()

    def test_incomplete_read_never_accepted(self):
        store = r.RecoveryStore(self.root, self.metadata)
        with self.assertRaisesRegex(FirmwareError, 'firmware_read_incomplete'):
            store.capture(lambda *_: b'x', lambda *_: None)

    def test_low_disk_and_missing_private_memory_refuse_before_read(self):
        store = r.RecoveryStore(self.root, self.metadata)
        with patch.object(r.shutil, 'disk_usage', return_value=type('Space', (), {'free': 1})()):
            with self.assertRaisesRegex(FirmwareError, 'recovery_disk_full'): store.capture(lambda *_: self.fail('read'), lambda *_: None)
        with patch.object(r, 'require_no_swap', side_effect=FirmwareError('private_memory_unavailable')):
            with self.assertRaises(FirmwareError): r.RecoveryStore(Path(self.tmp.name) / 'other', self.metadata)

    def test_nested_windows_locks_do_not_unlock_outer_pages(self):
        kernel = MagicMock()
        kernel.VirtualLock.return_value = True
        data = bytearray(8192)
        with patch.object(r.sys, 'platform', 'win32'), patch.object(r.ctypes, 'WinDLL', return_value=kernel, create=True):
            with r.locked(data):
                calls = kernel.VirtualLock.call_count
                with r.locked(memoryview(data)[:64]):
                    self.assertEqual(kernel.VirtualLock.call_count, calls)
                kernel.VirtualUnlock.assert_not_called()
            self.assertEqual(kernel.VirtualUnlock.call_count, calls)
        self.assertEqual(r._LOCKED_PAGES, {})

    def test_failed_windows_lock_never_yields_private_buffer(self):
        kernel = MagicMock(); kernel.VirtualLock.return_value = False
        with patch.object(r.sys, 'platform', 'win32'), patch.object(r.ctypes, 'WinDLL', return_value=kernel, create=True):
            with self.assertRaisesRegex(FirmwareError, 'private_memory_unavailable'):
                with r.locked(bytearray(1024)): self.fail('Unavailable protection was bypassed')
        self.assertEqual(r._LOCKED_PAGES, {})

    @unittest.skipUnless(sys.platform == 'win32', 'Native Windows security API acceptance')
    def test_windows_dpapi_roundtrip_and_locked_buffer(self):
        key = b'synthetic-private-key' * 2
        encrypted = r.dpapi(key)
        self.assertNotIn(key, encrypted)
        self.assertEqual(r.dpapi(encrypted, decrypt=True), key)
        with r.locked(bytearray(key)) as value: self.assertEqual(value, key)

    @unittest.skipUnless(os.name == 'posix', 'POSIX filesystem boundaries')
    def test_symlink_and_public_directory_refused(self):
        self.root.mkdir(mode=0o755)
        self.root.chmod(0o755)
        with self.assertRaises(FirmwareError): r.private_directory(self.root)
        linked = Path(self.tmp.name) / 'linked'; linked.symlink_to(self.root)
        with self.assertRaises(FirmwareError): r.private_directory(linked)


if __name__ == '__main__': unittest.main()
