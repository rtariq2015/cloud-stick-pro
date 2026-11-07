"""Synthetic first-owner tests. No serial, Bluetooth, account or files are used."""
import asyncio
import ast
import json
from pathlib import Path
import re
import struct
import threading
import unittest
import zlib
from types import SimpleNamespace as NS

from cloudstick_desktop.enrollment import (GROUP, OWNER_MISSING, OWNER_READY,
                                           owner_identity, owner_status, validate_record)
from cloudstick_desktop.protocol import TransportError, validate_request
from cloudstick_desktop.transports import DesktopTransport, NativeUSB
from test_transports import BLE, STATUS


DEVICE = "001122334455"
FINGERPRINT = (0x303A, 0x1001, "synthetic-unit", "synthetic-port")
GATE = dict(owner_state=1, boot_down=True, held_ms=10000, blocked=0,
            can_enroll=True, last_blocked=0, install_step=0)
READY = dict(GATE, owner_state=0, blocked=1, can_enroll=False, install_step=6)


def record():
    value = bytearray(432)
    value[:6] = b"COW2\x02\x00"
    value[6:12] = bytes.fromhex(DEVICE)
    value[12] = value[28] = 1
    struct.pack_into("<I", value, 428, zlib.crc32(value[:428]))
    return value.hex()


def changed_record(offset, value, *, crc=True):
    raw = bytearray.fromhex(record())
    raw[offset] = value
    if crc:
        struct.pack_into("<I", raw, 428, zlib.crc32(raw[:428]))
    return raw.hex()


class Stop(threading.Event):
    def wait(self, timeout=None):
        return self.is_set()


class Clock:
    def __init__(self):
        self.now = 0.0
        self.waits = []

    def read(self):
        return self.now

    def wait(self, delay):
        self.waits.append(delay)
        self.now += delay
        return False


class Serial:
    def __init__(self, stop):
        self.stop = stop
        self.events, self.commands = [], []
        self.buffer = bytearray()
        self.closed = self.installed = False
        self.gate = dict(GATE)
        self.ready = dict(READY)
        self.device = DEVICE
        self.status = dict(STATUS, firmware="0.4.1-setup-dev", owner_ready=False,
                           backups_paused=True)
        self.install_mode = "ok"
        self.on_write = lambda command: None

    def __setattr__(self, name, value):
        if name in ("dtr", "rts", "port") and "events" in self.__dict__:
            self.events.append((name, value))
        object.__setattr__(self, name, value)

    def open(self):
        self.events.append(("open", self.port))

    def reset_input_buffer(self):
        self.events.append(("drain",))
        self.buffer.clear()

    def write(self, payload):
        command = bytes(payload).decode("ascii").rstrip("\n")
        # Fixture-only command storage contains solely the public synthetic record.
        self.commands.append(command)
        self.on_write(command)
        if command == "STATUS":
            rows = [self.status, {"wifi_step": 0, "wifi_sdk_error": 0, "wifi_error": 0},
                    {"ble_before_free": 123, "host_exit_stack_min": 456}]
        elif command == "OWNER_ID":
            rows = [{"owner_device": self.device}]
        elif command == "OWNER_STATUS":
            rows = [self.ready if self.installed else self.gate]
        elif command.startswith("OWNER_INSTALL "):
            self.installed = True
            if self.install_mode == "short":
                return len(payload) - 1
            if self.install_mode == "lost":
                return len(payload)
            if self.install_mode == "cancel":
                self.stop.set()
            rows = [{"owner_installed": self.install_mode != "false"}]
        else:
            raise AssertionError("Unexpected synthetic command")
        self.buffer.extend(b"".join(json.dumps(row).encode() + b"\n" for row in rows))
        return len(payload)

    def read(self, maximum):
        if not self.buffer:
            raise OSError("synthetic response loss")
        count = min(37, maximum, len(self.buffer))
        result = bytes(self.buffer[:count])
        del self.buffer[:count]
        return result

    def close(self):
        self.closed = True


class CodecTests(unittest.TestCase):
    def test_record_matches_firmware_group_and_crc_format(self):
        source = Path(__file__).resolve().parents[2] / "lib/wifi_setup_model/owner_group.h"
        expected = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", source.read_text()))
        self.assertEqual(GROUP, expected)
        self.assertEqual(validate_record(record(), DEVICE), DEVICE)

    def test_exact_record_shape_bounds_and_device(self):
        for value in (None, 1, "", record()[:-2], record() + "00", record().upper(), "gg" * 432,
                      changed_record(0, 0), changed_record(4, 1), changed_record(5, 1),
                      changed_record(12, 0), changed_record(28, 0), changed_record(412, 1),
                      changed_record(428, 1, crc=False)):
            with self.subTest(shape=type(value).__name__), self.assertRaises(TransportError):
                validate_record(value)
        with self.assertRaises(TransportError) as caught:
            validate_record(record(), "aabbccddeeff")
        self.assertEqual(caught.exception.code, "owner_record_device_mismatch")

    def test_verifier_equal_or_above_prime_is_rejected(self):
        raw = bytearray.fromhex(record())
        for verifier in (GROUP, b"\xff" * 384):
            raw[28:412] = verifier
            struct.pack_into("<I", raw, 428, zlib.crc32(raw[:428]))
            with self.assertRaises(TransportError):
                validate_record(raw.hex())

    def test_status_exact_types_and_physical_gate_consistency(self):
        self.assertEqual(owner_status(GATE), GATE)
        self.assertEqual(owner_status(READY), READY)
        invalid = [dict(GATE, extra="ignored"), dict(GATE, owner_state=True), dict(GATE, boot_down=1),
                   dict(GATE, owner_state=3), dict(GATE, held_ms=2**32), dict(GATE, held_ms=9999),
                   dict(GATE, blocked=1024), dict(GATE, can_enroll=1), dict(GATE, blocked=1),
                   dict(GATE, owner_state=0), dict(GATE, install_step=7), dict(GATE, boot_down=False)]
        for value in invalid:
            with self.assertRaises(TransportError):
                owner_status(value)

    def test_identity_exact_canonical_shape(self):
        self.assertEqual(owner_identity({"owner_device": DEVICE}), {"device": DEVICE})
        for value in ({"owner_device": DEVICE, "extra": 1}, {"owner_device": ""},
                      {"owner_device": "AA1122334455"}, {"owner_device": "000000000000"},
                      {"owner_device": "ff:ff:ff:ff:ff:ff"}, {"owner_device": True}):
            with self.assertRaises(TransportError):
                owner_identity(value)

    def test_strict_renderer_operations_exclude_credentials_paths_and_commands(self):
        for op in ("usb_owner_status", "usb_owner_identity", "usb_owner_install"):
            base = {"op": op, "port": "COM3"}
            if op.endswith("install"):
                base["record_hex"] = record()
            self.assertEqual(validate_request(base), op)
            for key in ("command", "username", "password", "path", "device"):
                with self.assertRaises(TransportError):
                    validate_request(dict(base, **{key: "forbidden"}))
        with self.assertRaises(TransportError):
            validate_request({"op": "usb_owner_install", "port": "COM3"})

    def test_bridge_allowlist_and_synthetic_report_never_echo_record(self):
        source = Path(__file__).resolve().parents[1] / "cloudstick_desktop/main.py"
        tree = ast.parse(source.read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "reportable_response")
        namespace = {"json": json}
        exec(compile(ast.Module(body=[function], type_ignores=[]), "synthetic-redactor", "exec"), namespace)
        result = namespace["reportable_response"](json.dumps({"id": 1, "ok": True,
                                                             "result": {"record_hex": record()}}))
        self.assertEqual(result["error"]["code"], "sensitive_response_omitted")
        self.assertNotIn(record(), json.dumps(result))
        strings = {node.value for node in ast.walk(tree) if isinstance(node, ast.Constant) and isinstance(node.value, str)}
        for op in ("usb_owner_status", "usb_owner_identity", "usb_owner_install"):
            self.assertIn(op, strings)


class EnrollmentTests(unittest.TestCase):
    def setUp(self):
        self.stop = Stop()
        self.clock = Clock()
        self.serial = Serial(self.stop)
        self.port = NS(device="COM3", vid=FINGERPRINT[0], pid=FINGERPRINT[1],
                       serial_number=FINGERPRINT[2], location=FINGERPRINT[3])
        self.options = []
        def factory(**options):
            self.options.append(options)
            return self.serial
        self.usb = NativeUSB(factory, lambda: [self.port], lambda port: False, lambda port, mode: True,
                             owner_clock=self.clock.read, owner_wait=self.clock.wait)

    def call(self, op="usb_owner_install", payload=None):
        return self.usb.owner_exchange("COM3", FINGERPRINT, op, self.stop,
                                       record() if payload is None else payload)

    def installs(self):
        return sum(command.startswith("OWNER_INSTALL ") for command in self.serial.commands)

    def test_success_one_write_exact_order_fresh_ready_and_nonreset_sequence(self):
        self.assertEqual(self.call(), {"installed": True})
        self.assertEqual(self.serial.commands[:3], ["STATUS", "OWNER_ID", "OWNER_STATUS"])
        self.assertEqual(self.serial.commands[-1], "OWNER_STATUS")
        self.assertEqual(self.installs(), 1)
        self.assertEqual(self.serial.events[:5], [("dtr", False), ("rts", False), ("port", "COM3"),
                                                ("open", "COM3"), ("dtr", True)])
        self.assertEqual(self.options[0]["baudrate"], 115200)
        self.assertTrue(self.serial.closed)

    def test_status_and_identity_are_read_only_and_typed(self):
        self.assertEqual(self.call("usb_owner_status"), GATE)
        self.assertEqual(self.call("usb_owner_identity"), {"device": DEVICE})
        self.assertEqual(self.installs(), 0)

    def test_all_exact_reviewed_profiles_support_the_same_owner_protocol(self):
        for firmware in ("0.4.1-setup-dev", "0.4.2-incremental-dev", "0.4.3-ui-dev", "0.4.4-wifi-dev", "0.4.5-ble-dev"):
            with self.subTest(firmware=firmware):
                self.setUp()
                self.serial.status["firmware"] = firmware
                self.assertEqual(self.call("usb_owner_status"), GATE)
                self.assertEqual(self.installs(), 0)
                self.assertEqual(self.call(), {"installed": True})
                self.assertEqual(self.installs(), 1)

    def test_incremental_admission_preserves_existing_owner_and_physical_guards(self):
        cases = (("owner_state", OWNER_READY), ("owner_state", 2),
                 ("boot_down", False), ("held_ms", 9999), ("blocked", 2))
        for key, value in cases:
            with self.subTest(key=key, value=value):
                self.setUp()
                self.serial.status["firmware"] = "0.4.2-incremental-dev"
                self.serial.gate[key] = value
                with self.assertRaises(TransportError):
                    self.call()
                self.assertEqual(self.installs(), 0)

    def test_ready_and_fault_never_write(self):
        for state in (0, 2):
            self.serial.gate = dict(GATE, owner_state=state, blocked=1, can_enroll=False)
            with self.assertRaises(TransportError) as caught:
                self.call()
            self.assertEqual(caught.exception.code, "owner_not_missing")
        self.assertEqual(self.installs(), 0)

    def test_every_firmware_physical_blocker_refuses_install(self):
        for mask in (2, 4, 8, 16, 32, 64, 128, 256, 512):
            self.serial.gate = dict(GATE, blocked=mask, can_enroll=False)
            with self.assertRaises(TransportError):
                self.call()
        self.assertEqual(self.installs(), 0)

    def test_general_store_ble_and_worker_guards(self):
        for field, value in (("store_fault", True), ("ble", True), ("backups_paused", False)):
            self.serial.status = dict(STATUS, firmware="0.4.1-setup-dev", owner_ready=False,
                                      backups_paused=True)
            self.serial.status[field] = value
            with self.assertRaises(TransportError):
                self.call()
        self.assertEqual(self.installs(), 0)

    def test_unreviewed_or_stock_firmware_never_sends_owner_commands(self):
        for firmware in ("0.3.3-dev", "0.4.0-rc5", "0.4.1", "0.4.2", "0.4.2-unknown",
                         "0.4.2-incremental-dev.1", "0.4.3-incremental-dev",
                         "0.4.4", "0.4.4-wifi-dev.1", "0.4.4-ui-dev",
                         "0.4.5", "0.4.5-ble-dev.1", "0.4.5-wifi-dev"):
            with self.subTest(firmware=firmware):
                self.setUp()
                self.serial.status["firmware"] = firmware
                with self.assertRaises(TransportError) as caught:
                    self.call()
                self.assertEqual(caught.exception.code, "owner_firmware_unsupported")
                self.assertEqual(self.serial.commands, ["STATUS"])

    def test_no_serial_number_or_reused_port_refused_before_open(self):
        for fingerprint in ((0x303A, 0x1001, None, "location"), (0x303A, 0x1001, "other", "x")):
            with self.assertRaises(TransportError):
                self.usb.owner_exchange("COM3", fingerprint, "usb_owner_install", self.stop, record())
        self.assertEqual(self.options, [])

    def test_record_invalid_before_open_and_actual_mac_rechecked(self):
        with self.assertRaises(TransportError):
            self.call(payload="00")
        self.assertEqual(self.options, [])
        self.serial.device = "aabbccddeeff"
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "owner_record_device_mismatch")
        self.assertEqual(self.installs(), 0)

    def test_usb_replacement_before_install_never_writes(self):
        def change(command):
            if command == "OWNER_STATUS":
                self.port.serial_number = "replaced"
        self.serial.on_write = change
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "usb_device_changed")
        self.assertEqual(self.installs(), 0)

    def test_ack_loss_short_write_rejection_or_cancel_stays_unknown_no_retry(self):
        for mode in ("short", "lost", "false", "cancel"):
            self.setUp()
            self.serial.install_mode = mode
            for attempt in range(2):
                with self.assertRaises(TransportError) as caught:
                    self.call()
                self.assertEqual(caught.exception.code, "owner_install_unknown")
            self.assertEqual(self.installs(), 1)
            self.assertTrue(self.serial.closed)

    def test_post_ack_missing_fault_or_unverified_readback_is_unknown(self):
        for ready in (GATE, dict(READY, owner_state=2), dict(READY, install_step=5)):
            self.setUp()
            self.serial.ready = ready
            with self.assertRaises(TransportError) as caught:
                self.call()
            self.assertEqual(caught.exception.code, "owner_install_unknown")
            self.assertEqual(self.installs(), 1)

    def test_cancellation_before_write_preserves_missing_and_no_install(self):
        self.serial.on_write = lambda command: self.stop.set() if command == "OWNER_STATUS" else None
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "cancelled")
        self.assertEqual(self.installs(), 0)

    def test_native_close_failure_retains_session_blocks_reopen_and_is_unknown(self):
        def fail_close():
            raise OSError("synthetic private provider detail")
        self.serial.close = fail_close
        with self.assertRaises(TransportError) as caught:
            self.call()
        self.assertEqual(caught.exception.code, "owner_install_unknown")
        self.assertIs(self.usb._retained_session, self.serial)
        count = len(self.options)
        with self.assertRaises(TransportError):
            self.call("usb_owner_status")
        self.assertEqual(len(self.options), count)
        self.assertEqual(self.installs(), 1)

    def test_actual_reply_rate_limit_is_respected_after_complete_response(self):
        previous = [None]
        sends = []
        # Simulate the firmware's real250ms silent drop rule without sleeping.
        def rate_limit(command):
            now = self.clock.read()
            if previous[0] is not None and now - previous[0] < .25:
                raise AssertionError("command would be silently dropped by firmware")
            sends.append((command.split(" ")[0], now))
            previous[0] = now
        self.serial.on_write = rate_limit
        original_read = self.serial.read
        def slow_response(maximum):
            result = original_read(maximum)
            self.clock.now += .02  # Split STATUS arrives over measurable time.
            if not self.serial.buffer:
                previous[0] = self.clock.read()
            return result
        self.serial.read = slow_response
        self.assertEqual(self.call(), {"installed": True})
        self.assertEqual([row[0] for row in sends], ["STATUS", "OWNER_ID", "OWNER_STATUS", "OWNER_INSTALL", "OWNER_STATUS"])
        self.assertEqual(self.installs(), 1)
        self.assertTrue(self.clock.waits)
        self.assertTrue(all(0 < delay <= .05 for delay in self.clock.waits))

    def test_pacing_wait_cancellation_or_usb_replacement_prevents_next_command(self):
        for mode in ("cancel", "replace"):
            self.setUp()
            def wait(delay):
                self.clock.wait(delay)
                if mode == "cancel":
                    self.stop.set()
                else:
                    self.port.serial_number = "replaced"
                return self.stop.is_set()
            self.usb._owner_wait = wait
            with self.assertRaises(TransportError) as caught:
                self.call()
            self.assertEqual(caught.exception.code, "cancelled" if mode == "cancel" else "usb_device_changed")
            self.assertEqual(self.serial.commands, ["STATUS"])
            self.assertEqual(self.installs(), 0)


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.stop = Stop()
        self.clock = Clock()
        self.serial = Serial(self.stop)
        self.port = NS(device="COM3", vid=FINGERPRINT[0], pid=FINGERPRINT[1],
                       serial_number=FINGERPRINT[2], location=FINGERPRINT[3])
        self.usb = NativeUSB(lambda **opts: self.serial, lambda: [self.port], lambda p: False, lambda p, m: True,
                             owner_clock=self.clock.read, owner_wait=self.clock.wait)
        self.transport = DesktopTransport(ble=BLE(), usb=self.usb, operation_timeout=2, cleanup_timeout=.1)
        await self.transport.dispatch({"op": "usb_ports"})

    async def asyncTearDown(self):
        await self.transport.dispatch({"op": "disconnect"})

    async def test_identity_and_install_arm_exact_target_pin_disconnect_clears(self):
        result = await self.transport.dispatch({"op": "usb_owner_identity", "port": "COM3"})
        self.assertEqual(result, {"device": DEVICE})
        self.assertEqual(self.transport._setup_device_id, "00:11:22:33:44:55")
        await self.transport.dispatch({"op": "disconnect"})
        self.assertIsNone(self.transport._setup_device_id)
        result = await self.transport.dispatch({"op": "usb_owner_install", "port": "COM3", "record_hex": record()})
        self.assertEqual(result, {"installed": True})
        self.assertEqual(self.transport._setup_device_id, "00:11:22:33:44:55")

    async def test_canceled_blocking_install_retains_job_and_reports_unknown(self):
        started, release = threading.Event(), threading.Event()
        def blocked(*args):
            started.set()
            release.wait(1)
            return {"installed": True}
        self.usb.owner_exchange = blocked
        pending = asyncio.create_task(self.transport.dispatch({"op": "usb_owner_install", "port": "COM3", "record_hex": record()}))
        while not started.is_set():
            await asyncio.sleep(.001)
        pending.cancel()
        with self.assertRaises(TransportError) as caught:
            await pending
        self.assertEqual(caught.exception.code, "owner_install_unknown")
        with self.assertRaises(TransportError) as busy:
            await self.transport.dispatch({"op": "usb_owner_status", "port": "COM3"})
        self.assertEqual(busy.exception.code, "transport_busy")
        self.assertIsNone(self.transport._setup_device_id)
        release.set()
        await self.transport.dispatch({"op": "disconnect"})


if __name__ == "__main__":
    unittest.main()
