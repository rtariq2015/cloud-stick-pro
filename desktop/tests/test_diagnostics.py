"""Synthetic diagnostic serial tests. No device, radio, cloud or private files."""
import asyncio
import copy
import json
import threading
import unittest
from types import SimpleNamespace as NS

from cloudstick_desktop.diagnostics import (BACKUP_BOOLS, BACKUP_INTS, DiagnosticsChannel,
                                           LIFECYCLE_BOOLS, LIFECYCLE_INTS, MEMORY_BOOLS,
                                           MEMORY_INTS, dashboard_row, status_rows, typed)
from cloudstick_desktop.protocol import TransportError, validate_request
from cloudstick_desktop.transports import DesktopTransport, NativeUSB
from test_enrollment import Clock, Stop, FINGERPRINT
from test_transports import BLE, STATUS


STATUS_ROWS = [dict(STATUS, firmware="0.4.1-setup-dev"),
               {"wifi_step": 15, "wifi_sdk_error": 0, "wifi_error": 0},
               {**{key: False for key in LIFECYCLE_BOOLS}, **{key: 54321 for key in LIFECYCLE_INTS}}]
BOOT = {"uptime_ms": 123456, "reset_reason": 1}
MEMORY = {**{key: False for key in MEMORY_BOOLS}, **{key: 34567 for key in MEMORY_INTS}}
BACKUP = {**{key: False for key in BACKUP_BOOLS}, **{key: 0 for key in BACKUP_INTS}}
BACKUP.update(cloud_configured=True, has_scan=True, scan_complete=True, verified=5,
              last_backup_unix=1794000000, done=2**63 + 1, total=2**64 - 1)
DASHBOARD = {"dashboard_listening": True, "dashboard_fault": False, "station_ip": "192.0.2.21"}


class Serial:
    def __init__(self, clock):
        self.clock = clock
        self.commands, self.events = [], []
        self.rows = {"STATUS": copy.deepcopy(STATUS_ROWS), "BOOT_STATUS": [dict(BOOT)],
                     "MEMORY_STATUS": [dict(MEMORY)], "BACKUP_STATUS": [dict(BACKUP)],
                     "DASHBOARD_STATUS": [dict(DASHBOARD)]}
        self.buffer = bytearray()
        self.closed = False
        self.previous_reply = 0.0
        self.on_write = lambda command: None
        self.on_close = lambda: None
        self.raw = None
        self.short = False

    def __setattr__(self, name, value):
        if name in ("dtr", "rts", "port") and "events" in self.__dict__:
            self.events.append((name, value))
        object.__setattr__(self, name, value)

    def open(self):
        self.events.append(("open", self.port))

    def reset_input_buffer(self):
        self.buffer.clear()

    def write(self, data):
        command = bytes(data).decode("ascii").rstrip("\n")
        # Same silent-drop threshold as the actual firmware, including first send.
        if self.clock.read() - self.previous_reply < .25:
            raise AssertionError("synthetic firmware would drop an unpaced command")
        self.commands.append(command)
        self.on_write(command)
        if self.short:
            return len(data) - 1
        self.buffer.extend(self.raw if self.raw is not None else b"".join(
            json.dumps(row).encode() + b"\n" for row in self.rows[command]))
        return len(data)

    def read(self, maximum):
        if not self.buffer:
            self.clock.now += .1
            return b""
        size = min(maximum, 41, len(self.buffer))
        chunk = bytes(self.buffer[:size])
        del self.buffer[:size]
        self.clock.now += .01
        if not self.buffer:
            self.previous_reply = self.clock.read()
        return chunk

    def close(self):
        self.closed = True
        self.on_close()


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.clock, self.stop = Clock(), Stop()
        self.serial = Serial(self.clock)
        self.port = NS(device="COM3", vid=FINGERPRINT[0], pid=FINGERPRINT[1],
                       serial_number=FINGERPRINT[2], location=FINGERPRINT[3])
        self.created = []
        def factory(**options):
            self.created.append(options)
            return self.serial
        self.usb = NativeUSB(factory, lambda: [self.port], lambda port: False, lambda p, m: True,
                             owner_clock=self.clock.read, owner_wait=self.clock.wait)

    def call(self):
        return self.usb.diagnostics("COM3", FINGERPRINT, self.stop)

    def test_full_snapshot_exact_read_only_commands_and_rate_limit(self):
        result = self.call()
        self.assertEqual(self.serial.commands, ["STATUS", "BOOT_STATUS", "MEMORY_STATUS", "BACKUP_STATUS", "DASHBOARD_STATUS"])
        self.assertEqual(set(result), {"schema", "snapshot_kind", "status", "radio", "lifecycle", "boot", "memory", "backup", "dashboard"})
        self.assertEqual(result["schema"], "cloud-stick-diagnostics/v1")
        self.assertEqual(result["snapshot_kind"], "sequential_read_only")
        self.assertEqual(result["backup"]["done"], str(2**63 + 1))
        self.assertEqual(result["backup"]["total"], str(2**64 - 1))
        self.assertEqual(result["dashboard"], DASHBOARD)
        self.assertGreaterEqual(self.clock.now, 5 * .35)
        self.assertTrue(self.serial.closed)
        self.assertEqual(self.created[0]["baudrate"], 115200)
        self.assertEqual(self.serial.events[:5], [("dtr", False), ("rts", False), ("port", "COM3"), ("open", "COM3"), ("dtr", True)])

    def test_busy_or_paused_device_facts_are_reported_without_mutation_or_cloud_claim(self):
        self.serial.rows["STATUS"][0].update(ble=True, backups_paused=True)
        self.serial.rows["BACKUP_STATUS"][0].update(paused=True, worker_phase=3, active=True)
        result = self.call()
        self.assertTrue(result["status"]["ble"])
        self.assertTrue(result["backup"]["paused"])
        self.assertFalse(result["status"]["internet_verified"])
        self.assertTrue(result["backup"]["cloud_configured"])
        self.assertFalse(any(term in json.dumps(result) for term in ("password", "verifier", "SSID", "filename")))

    def test_only_one_selected_port_and_no_renderer_command_payload(self):
        self.assertEqual(validate_request({"op": "usb_diagnostics", "port": "COM3"}), "usb_diagnostics")
        for field in ("command", "path", "password", "record_hex", "device_id"):
            with self.assertRaises(TransportError):
                validate_request({"op": "usb_diagnostics", "port": "COM3", field: "forbidden"})

    def test_internal_readonly_allowlist_refuses_every_mutating_command(self):
        channel = DiagnosticsChannel(self.serial, self.stop, lambda: True, self.usb._json,
                                     clock=self.clock.read, wait=self.clock.wait)
        for command in ("BENCH_WINDOW", "CHECK", "OWNER_INSTALL", "SETUP", "RESET", "STATUS\nCHECK"):
            with self.assertRaises(TransportError):
                channel.request(command)
        self.assertEqual(self.serial.commands, [])

    def test_unreviewed_firmware_refused_without_further_queries(self):
        for firmware in ("0.4.0-rc5", "0.4.1", "0.4.2", "0.4.2-unknown",
                         "0.4.2-incremental-dev.1", "0.4.3-incremental-dev",
                         "0.4.4", "0.4.4-wifi-dev.1", "0.4.4-ui-dev",
                         "0.4.5", "0.4.5-ble-dev.1", "0.4.5-wifi-dev",
                         "0.4.6", "0.4.6-cache-dev.1", "0.4.6-cache-lab"):
            with self.subTest(firmware=firmware):
                self.setUp()
                self.serial.rows["STATUS"][0]["firmware"] = firmware
                with self.assertRaises(TransportError) as caught:
                    self.call()
                self.assertEqual(caught.exception.code, "diagnostics_firmware_unsupported")
                self.assertEqual(self.serial.commands, ["STATUS"])

    def test_all_exact_reviewed_profiles_complete_the_same_diagnostic_snapshot(self):
        for firmware in ("0.4.1-setup-dev", "0.4.2-incremental-dev", "0.4.3-ui-dev", "0.4.4-wifi-dev", "0.4.5-ble-dev", "0.4.6-cache-dev"):
            with self.subTest(firmware=firmware):
                self.setUp()
                self.serial.rows["STATUS"][0]["firmware"] = firmware
                result = self.call()
                self.assertEqual(result["status"]["firmware"], firmware)
                self.assertEqual(self.serial.commands, ["STATUS", "BOOT_STATUS", "MEMORY_STATUS", "BACKUP_STATUS", "DASHBOARD_STATUS"])
                self.assertTrue(self.serial.closed)

    def test_malformed_schema_wrong_types_ranges_and_private_extra_fields_fail_closed(self):
        changes = [("BOOT_STATUS", "uptime_ms", True), ("BOOT_STATUS", "reset_reason", 256),
                   ("MEMORY_STATUS", "samples", 2**32), ("MEMORY_STATUS", "allocation_failure_seen", 1),
                   ("BACKUP_STATUS", "worker_phase", 6), ("BACKUP_STATUS", "phase", 5),
                   ("BACKUP_STATUS", "error", -2**31 - 1), ("BACKUP_STATUS", "done", 2**64),
                   ("BACKUP_STATUS", "last_backup_unix", 253402300800),
                   ("DASHBOARD_STATUS", "dashboard_fault", "false"),
                   ("BACKUP_STATUS", "filename", "PRIVATE_SYNTHETIC_NAME")]
        for command, key, value in changes:
            self.setUp()
            self.serial.rows[command][0][key] = value
            with self.assertRaises(TransportError) as caught:
                self.call()
            self.assertNotIn("PRIVATE_SYNTHETIC_NAME", str(caught.exception))
            self.assertTrue(self.serial.closed)

    def test_every_status_row_requires_exact_schema(self):
        for index, key, value in ((0, "ssid", "PRIVATE"), (0, "internet_verified", True),
                                  (1, "wifi_step", 29), (1, "wifi_sdk_error", 2**31),
                                  (2, "ble_recovery_checked", 1), (2, "host_exit_stack_min", -1)):
            rows = copy.deepcopy(STATUS_ROWS)
            rows[index][key] = value
            with self.assertRaises(TransportError):
                status_rows(rows)

    def test_canonical_ipv4_only_and_offline_address_allowed(self):
        self.assertEqual(dashboard_row(dict(DASHBOARD, station_ip="0.0.0.0"))["station_ip"], "0.0.0.0")
        for address in ("192.168.001.1", "https://example.com", "::1", "1.2.3.4\n", "256.1.2.3", 123, None):
            with self.assertRaises(TransportError):
                dashboard_row(dict(DASHBOARD, station_ip=address))

    def test_duplicate_json_overflow_and_partial_response_are_rejected(self):
        for raw in (b'{"uptime_ms":1,"uptime_ms":2}\n', b'{broken}\n', b'x' * 9000,
                    b'{"uptime_ms":123'):
            self.setUp()
            self.serial.raw = raw
            with self.assertRaises(TransportError):
                self.call()
            self.assertTrue(self.serial.closed)

    def test_short_write_is_not_retried(self):
        self.serial.short = True
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "usb_write_failed")
        self.assertEqual(self.serial.commands, ["STATUS"])
        self.assertTrue(self.serial.closed)

    def test_changed_selected_usb_before_during_and_after_close_is_rejected(self):
        for phase in ("before", "during", "close"):
            self.setUp()
            if phase == "before":
                self.port.serial_number = "replaced"
            elif phase == "during":
                self.serial.on_write = lambda command: setattr(self.port, "serial_number", "replaced") if command == "BOOT_STATUS" else None
            else:
                self.serial.on_close = lambda: setattr(self.port, "serial_number", "replaced")
            with self.assertRaises(TransportError) as caught:
                self.call()
            self.assertEqual(caught.exception.code, "usb_device_changed")

    def test_cancellation_during_pacing_stops_before_next_send(self):
        def wait(delay):
            self.clock.wait(delay)
            if self.serial.commands:
                self.stop.set()
            return self.stop.is_set()
        self.usb._owner_wait = wait
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "cancelled")
        self.assertEqual(self.serial.commands, ["STATUS"])
        self.assertTrue(self.serial.closed)

    def test_native_close_failure_retains_provider_and_blocks_reopening(self):
        def failure():
            raise OSError("PRIVATE_SYNTHETIC_PROVIDER_DETAIL")
        self.serial.on_close = failure
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "usb_cleanup_unknown")
        self.assertIs(self.usb._retained_session, self.serial)
        count = len(self.created)
        with self.assertRaises(TransportError):
            self.call()
        self.assertEqual(len(self.created), count)


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.ble = BLE()
        self.started, self.release = threading.Event(), threading.Event()
        class USB:
            def ports(inner):
                return {"COM3": FINGERPRINT}
            def diagnostics(inner, port, fingerprint, stop):
                self.started.set()
                self.release.wait(1)
                return {"schema": "synthetic-only"}
        self.usb = USB()
        self.transport = DesktopTransport(ble=self.ble, usb=self.usb, operation_timeout=2, cleanup_timeout=1)
        await self.transport.dispatch({"op": "usb_ports"})

    async def asyncTearDown(self):
        self.release.set()
        await self.transport.dispatch({"op": "disconnect"})

    async def test_cancel_and_timeout_retain_thread_and_cannot_return_stale_snapshot(self):
        pending = asyncio.create_task(self.transport.dispatch({"op": "usb_diagnostics", "port": "COM3"}))
        while not self.started.is_set():
            await asyncio.sleep(.001)
        pending.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await pending
        with self.assertRaises(TransportError) as busy:
            await self.transport.dispatch({"op": "usb_diagnostics", "port": "COM3"})
        self.assertEqual(busy.exception.code, "transport_busy")
        with self.assertRaises(TransportError) as busy_ble:
            await self.transport.dispatch({"op": "discover"})
        self.assertEqual(busy_ble.exception.code, "transport_busy")
        self.release.set()
        await self.transport.dispatch({"op": "disconnect"})

    async def test_active_native_ble_blocks_diagnostics(self):
        await self.transport.dispatch({"op": "discover"})
        await self.transport.dispatch({"op": "connect", "device_id": "device-1"})
        with self.assertRaises(TransportError) as caught:
            await self.transport.dispatch({"op": "usb_diagnostics", "port": "COM3"})
        self.assertEqual(caught.exception.code, "transport_busy")
        self.assertFalse(self.started.is_set())

    async def test_timeout_keeps_worker_owned_until_it_returns(self):
        # Only the deliberately blocked operation gets the short deadline.
        # Thread-pool startup during fixture discovery is unrelated to this
        # assertion and can exceed50ms on loaded native CI runners.
        self.transport._timeout = .05
        with self.assertRaises(TransportError) as caught:
            await self.transport.dispatch({"op": "usb_diagnostics", "port": "COM3"})
        self.assertEqual(caught.exception.code, "usb_timeout")
        with self.assertRaises(TransportError) as busy:
            await self.transport.dispatch({"op": "usb_ports"})
        self.assertEqual(busy.exception.code, "transport_busy")
        self.release.set()
        await self.transport.dispatch({"op": "disconnect"})

    async def test_explicit_disconnect_invalidates_late_successful_snapshot(self):
        pending = asyncio.create_task(self.transport.dispatch({"op": "usb_diagnostics", "port": "COM3"}))
        while not self.started.is_set():
            await asyncio.sleep(.001)
        closing = asyncio.create_task(self.transport.dispatch({"op": "disconnect"}))
        await asyncio.sleep(.001)
        self.release.set()
        await closing
        with self.assertRaises(TransportError) as caught:
            await pending
        self.assertEqual(caught.exception.code, "cancelled")


if __name__ == "__main__":
    unittest.main()
