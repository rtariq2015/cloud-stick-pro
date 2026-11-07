"""Native approval/reattach orchestration with synthetic packages and workers."""
import asyncio
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cloudstick_desktop import firmware_manager as m
from cloudstick_desktop.firmware_catalog import FirmwareError
from cloudstick_desktop.protocol import TransportError, validate_request
from cloudstick_desktop.transports import DesktopTransport


class USB:
    def ports(self): return {'COM3': (0x303a, 0x1001, '112233445566', 'synthetic')}


class ManagerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'private'; self.root.mkdir(mode=0o700)
        self.patch = patch.object(m, 'data_root', return_value=self.root); self.patch.start(); self.addCleanup(self.patch.stop)
        self.manager = m.FirmwareManager(USB())
        self.manager.bundle_path = Path(self.tmp.name) / 'synthetic.cspfw'; self.manager.bundle_path.write_bytes(b'synthetic')
        p = patch.object(m, 'process_identity', return_value=12345); p.start(); self.addCleanup(p.stop)
        p = patch.object(m, 'load_bundle', return_value={}); p.start(); self.addCleanup(p.stop)

    def available(self):
        with patch.object(m, 'download', side_effect=FirmwareError('firmware_download_failed')):
            self.manager.check()
            deadline = time.monotonic() + 3
            while self.manager.running and time.monotonic() < deadline: time.sleep(.01)
        self.assertEqual(self.manager.state()['phase'], 'available')
        self.manager.ports()

    def test_no_renderer_authority_and_approval_bound_to_request(self):
        self.available()
        request = {'id': 1, 'op': 'firmware_start', 'port': 'COM3', 'mode': 'new'}
        with self.assertRaisesRegex(FirmwareError, 'firmware_native_approval_required'): self.manager.start(request, 'forged')
        token, target = self.manager.offer(request)
        self.assertEqual(target, '0.4.6-cache-dev')
        with self.assertRaisesRegex(FirmwareError, 'firmware_native_approval_required'): self.manager.start(dict(request, id=2), token)

    def test_approval_is_single_use_and_persists_a_reviewed_job(self):
        self.available()
        request = {'id': 1, 'op': 'firmware_start', 'port': 'COM3', 'mode': 'new'}
        token, _ = self.manager.offer(request)
        with patch.object(m, 'launch_worker', return_value=1234) as launch, patch.object(m, 'worker_alive', return_value=True):
            self.manager.start(request, token); launch.assert_called_once()
            self.assertTrue(self.manager.blocked)
            directory = self.root / self.manager.job['name']
            config = json.loads((directory / 'job.json').read_bytes())
            self.assertEqual(config['mode'], 'new'); self.assertIsNone(config['prepared'])
            restored = m.FirmwareManager(USB())
            self.assertEqual(restored.state()['phase'], 'starting')
            self.assertTrue(restored.running)
        with self.assertRaises(FirmwareError): self.manager.start(request, token)

    def test_existing_update_cannot_skip_pause_or_change_board(self):
        self.available()
        request = {'id': 1, 'op': 'firmware_start', 'port': 'COM3', 'mode': 'existing'}
        with self.assertRaises(FirmwareError): self.manager.offer(request)
        request['mode'] = 'new'; token, _ = self.manager.offer(request)
        with patch.object(self.manager.usb, 'ports', return_value={'COM3': (0x303a, 0x1001, 'different', 'synthetic')}):
            with self.assertRaisesRegex(FirmwareError, 'firmware_device_changed'): self.manager.start(request, token)

    def test_untrusted_request_fields_rejected_before_native_logic(self):
        for field in ('path','approved','offset','url','signature','bytes'):
            with self.subTest(field=field), self.assertRaises(TransportError):
                validate_request({'op': 'firmware_start', 'port': 'COM3', 'mode': 'new', field: 'forged'})

    def test_worker_gone_after_write_never_becomes_success_or_automatic_retry(self):
        self.available()
        req = {'id': 1, 'op': 'firmware_start', 'port': 'COM3', 'mode': 'new'}
        token, _ = self.manager.offer(req)
        with patch.object(m, 'launch_worker', return_value=1234), patch.object(m, 'worker_alive', return_value=False):
            self.manager.start(req, token)
            path = self.root / self.manager.job['name'] / 'progress.json'
            from cloudstick_desktop.firmware_recovery import save_private
            save_private(path, json.dumps({'phase': 'writing', 'done': 0, 'total': 1, 'code': '', 'writing': True}).encode())
            with patch.object(m.time, 'time', return_value=time.time() + 60):
                state = self.manager.state()
            self.assertEqual(state['phase'], 'recovery_required'); self.assertTrue(state['can_resume'])
            with self.assertRaises(FirmwareError): self.manager.check()


class ConcurrencyTests(unittest.IsolatedAsyncioTestCase):
    async def test_renderer_start_dispatch_has_no_native_approval(self):
        transport = DesktopTransport(usb=USB())
        with self.assertRaisesRegex(TransportError, 'The local device operation ended') as caught:
            await transport.dispatch({'op':'firmware_start','port':'COM3','mode':'new'})
        self.assertEqual(caught.exception.code, 'firmware_native_approval_required')

    async def test_native_launch_blocks_overlapping_usb_before_background_handoff(self):
        transport = DesktopTransport(usb=USB())
        class Manager:
            blocked = False
            def start(self, *_):
                gate.wait(2); return {'phase':'starting'}
        gate = threading.Event(); transport._firmware = Manager()
        task = asyncio.create_task(transport.start_firmware({'op':'firmware_start','port':'COM3','mode':'new'}, 'native-token'))
        await asyncio.sleep(.01)
        try:
            with self.assertRaises(TransportError) as caught: await transport.dispatch({'op':'usb_ports'})
            self.assertEqual(caught.exception.code, 'firmware_busy')
        finally:
            gate.set(); await task

class FutureAdmissionTests(unittest.TestCase):
    def test_only_verified_catalog_can_extend_native_management_versions(self):
        from cloudstick_desktop.transports import NativeUSB
        from cloudstick_desktop.firmware_catalog import Catalog, KnownApp
        usb = NativeUSB()
        self.assertNotIn('0.4.7-test', usb._reviewed_firmware)
        with self.assertRaises(FirmwareError): usb.admit_catalog(b'{"signature_verified":true,"version":"0.4.7-test"}')
        self.assertNotIn('0.4.7-test', usb._reviewed_firmware)
        synthetic = Catalog(2, (), (KnownApp('0.4.7-test', 64, 'a'*64),), (), 'b'*64)
        with patch('cloudstick_desktop.firmware_catalog.load_catalog', return_value=synthetic) as verify:
            usb.admit_catalog(b'SYNTHETIC-SIGNED-CATALOG')
        verify.assert_called_once_with(b'SYNTHETIC-SIGNED-CATALOG')
        self.assertIn('0.4.7-test', usb._reviewed_firmware)
        from cloudstick_desktop.enrollment import OwnerChannel
        from test_diagnostics import STATUS_ROWS
        import copy
        rows = copy.deepcopy(STATUS_ROWS); rows[0]['firmware'] = '0.4.7-test'
        channel = OwnerChannel(None, threading.Event(), lambda: True, json.loads,
                               reviewed_firmware=usb._reviewed_firmware)
        channel.request = lambda *_, **__: rows
        self.assertEqual(channel.firmware()['firmware'], '0.4.7-test')


    def test_future_version_still_requires_exact_management_shapes(self):
        from cloudstick_desktop.diagnostics import status_rows
        from test_diagnostics import STATUS_ROWS
        import copy
        rows = copy.deepcopy(STATUS_ROWS)
        rows[0]['firmware'] = '0.4.7-test'
        with self.assertRaises(TransportError): status_rows(rows)
        self.assertEqual(status_rows(rows, reviewed_firmware={'0.4.7-test'})[0]['firmware'], '0.4.7-test')
        rows[0]['private_extra'] = 'SYNTHETIC'
        with self.assertRaises(TransportError): status_rows(rows, reviewed_firmware={'0.4.7-test'})


if __name__ == '__main__': unittest.main()
