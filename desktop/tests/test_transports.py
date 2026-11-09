"""Offline native-provider tests. No radio, serial port or network is opened."""

import asyncio
import json
import os
import threading
import unittest
from types import SimpleNamespace as NS
from unittest.mock import patch

from cloudstick_desktop.protocol import (CHARACTERISTICS, SERVICE, TransportError,
                                        diagnostic_status, validate_request)
from cloudstick_desktop.transports import DesktopTransport, NativeUSB

UUID = sorted(CHARACTERISTICS)[0]
STATUS = dict(firmware="0.4.0-rc5", setup_only=False, owner_ready=True,
              store_fault=False, ble=False, wifi="local_connected", saved_revision="6",
              internet_verified=False, backups_paused=False, internal_free=123456, largest_block=56789)


class Service:
    def __init__(self):
        self.characters = {uuid: NS(uuid=uuid, service_uuid=SERVICE) for uuid in CHARACTERISTICS}

    def get_characteristic(self, uuid):
        return self.characters.get(uuid)


class Client:
    def __init__(self, device, callback):
        self.device, self.callback = device, callback
        self.is_connected = False
        self.service = Service()
        self.services = NS(get_service=lambda uuid: self.service if uuid == SERVICE else None)
        self.connect_gate = self.read_gate = self.write_gate = self.disconnect_gate = None
        self.ignore_connect_cancel = False
        self.connect_started = asyncio.Event()
        self.read_started = asyncio.Event()
        self.disconnects = 0
        self.writes = []
        self.read_value = bytearray(b"\x00\xff")
        self.failure = None

    async def connect(self):
        self.connect_started.set()
        if self.connect_gate:
            try:
                await self.connect_gate.wait()
            except asyncio.CancelledError:
                if not self.ignore_connect_cancel:
                    raise
                await self.connect_gate.wait()
        if self.failure:
            raise self.failure
        self.is_connected = True

    async def disconnect(self):
        self.disconnects += 1
        if self.disconnect_gate:
            await self.disconnect_gate.wait()
        self.is_connected = False
        self.callback(self)

    async def read_gatt_char(self, characteristic):
        self.read_started.set()
        if self.read_gate:
            await self.read_gate.wait()
        if self.failure:
            raise self.failure
        return self.read_value

    async def write_gatt_char(self, characteristic, payload, *, response):
        self.writes.append((characteristic.uuid, payload, response))
        if self.write_gate:
            await self.write_gate.wait()
        if self.failure:
            raise self.failure


class BLE:
    def __init__(self):
        self.device = NS(address="device-1", name="CSP-W2-TEST")
        self.rows = {"device-1": (self.device, NS(service_uuids=[SERVICE], local_name=None))}
        self.clients = []
        self.configure = lambda client: None
        self.discovery_gate = None
        self.ignore_discovery_cancel = False

    async def discover(self, timeout):
        if self.discovery_gate:
            try:
                await self.discovery_gate.wait()
            except asyncio.CancelledError:
                if not self.ignore_discovery_cancel:
                    raise
                await self.discovery_gate.wait()
        return self.rows

    def client(self, device, disconnected_callback):
        client = Client(device, disconnected_callback)
        self.configure(client)
        self.clients.append(client)
        return client


class USB:
    def __init__(self):
        self.listed = {"COM3": (0x303A, 0x1001, "unit", "location")}
        self.calls = []
        self.started = threading.Event()
        self.release = threading.Event()
        self.block = False
        self.ignore_stop = False

    def ports(self):
        return self.listed.copy()

    def exchange(self, port, fingerprint, command, stop):
        self.calls.append((port, fingerprint, command))
        self.started.set()
        if self.block:
            while not self.release.wait(.005):
                if stop.is_set() and not self.ignore_stop:
                    raise TransportError("cancelled")
        if self.listed.get(port) != fingerprint:
            raise TransportError("usb_device_changed")
        return STATUS.copy() if command == "STATUS" else {"accepted": True}


class ProtocolTests(unittest.TestCase):
    def test_exact_request_contract_and_bounds(self):
        for request in [None, [], {}, {"op": []}, {"op": "erase"}, {"op": "connect"},
                        {"op": "discover", "timeout": 1}, {"op": "discover", "id": True},
                        {"op": "discover", "id": 2**53}, {"op": "read", "uuid": []},
                        {"op": "read", "uuid": SERVICE}, {"op": "connect", "device_id": "\n"},
                        {"op": "usb_status", "port": "COM3\nOWNER_INSTALL"},
                        {"op": "write", "uuid": UUID, "hex": ""},
                        {"op": "write", "uuid": UUID, "hex": "aa "},
                        {"op": "write", "uuid": UUID, "hex": "AA"},
                        {"op": "write", "uuid": UUID, "hex": "aa" * 513}]:
            with self.subTest(request=request), self.assertRaises(TransportError):
                validate_request(request)
        for request in [{"op": "disconnect"}, {"op": "read", "uuid": UUID, "id": 1},
                        {"op": "write", "uuid": UUID, "hex": "ff" * 512}]:
            self.assertEqual(validate_request(request), request["op"])

    def test_status_drops_nonallowlisted_private_fields(self):
        row = dict(STATUS, ssid="NEVER-RETURN", password="NEVER-RETURN", cloud_url="NEVER-RETURN")
        self.assertEqual(diagnostic_status(row), STATUS)

    def test_status_accepts_actual_setup_firmware_and_historical_versions(self):
        for firmware in ("0.4.1-setup-dev", "0.4.0-rc5", "0.4.0-rc4", "0.3.3-dev",
                         "0.3.3-public-wifi-mem1", "0.4.0-product-lab7", "0.4.1-alpha.2-dev", "0.4.1",
                         "999.999.999-" + "a" * 24):
            with self.subTest(firmware=firmware):
                row = dict(STATUS, firmware=firmware)
                self.assertEqual(diagnostic_status(row), row)

    def test_status_rejects_malformed_or_unbounded_firmware_labels(self):
        for firmware in ("0.4.1-", "0.4.1-.dev", "0.4.1-dev.", "0.4.1-dev..x",
                         "0.4.1-setup/dev", "0.4.1-<script>", "0.4.1-setup_dev", "0.4.1-dév",
                         "0.4.1-setup\x00dev", "0.4.1-setup\ndev", "0.4.1-setup\rdev",
                         "0.4.1-setup-dev/", "0.4.1-setup-dev\n", "1000.4.1-dev",
                         "0.4.1-" + "a" * 25, "0.4.1-" + "a" * 100000, None, 401):
            with self.subTest(firmware=str(firmware)[:50]), self.assertRaises(TransportError) as caught:
                diagnostic_status(dict(STATUS, firmware=firmware))
            self.assertEqual(caught.exception.code, "invalid_usb_response")

    def test_status_rejects_injected_strings_and_wrong_types(self):
        for key, value in [("firmware", "0.4.0\nsecret"), ("firmware", "secret"),
                           ("wifi", "secretSSID"), ("saved_revision", "2" * 21),
                           ("saved_revision", "18446744073709551616"),
                           ("internal_free", True), ("owner_ready", 1), ("largest_block", -1)]:
            with self.subTest(key=key), self.assertRaises(TransportError):
                diagnostic_status(dict(STATUS, **{key: value}))


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.ble, self.usb, self.events = BLE(), USB(), []
        self.transport = DesktopTransport(self.events.append, ble=self.ble, usb=self.usb,
                                          operation_timeout=.08, cleanup_timeout=.1)

    async def asyncTearDown(self):
        self.usb.release.set()
        if self.ble.discovery_gate:
            self.ble.discovery_gate.set()
        for client in self.ble.clients:
            for gate in (client.connect_gate, client.read_gate, client.write_gate, client.disconnect_gate):
                if gate:
                    gate.set()
        try:
            await self.transport.dispatch({"op": "disconnect"})
        except TransportError:
            pass
        if self.transport._background:
            await asyncio.wait(self.transport._background, timeout=.3)

    async def connect(self):
        self.assertEqual(await self.transport.dispatch({"op": "discover"}),
                         {"devices": [{"id": "device-1", "name": "CSP-W2-TEST"}]})
        self.assertEqual(await self.transport.dispatch({"op": "connect", "device_id": "device-1"}), {})
        return self.ble.clients[-1]

    async def expect_code(self, request, code):
        with self.assertRaises(TransportError) as caught:
            await self.transport.dispatch(request)
        self.assertEqual(caught.exception.code, code)
        self.assertNotIn("SECRET", str(caught.exception))

    async def test_unseen_device_cannot_connect(self):
        await self.expect_code({"op": "connect", "device_id": "device-1"}, "device_not_discovered")
        self.assertEqual(self.ble.clients, [])

    async def test_advertisement_service_filter_and_name_sanitization(self):
        self.ble.rows["foreign"] = (NS(address="foreign", name="CSP-W2-FOREIGN"), NS(service_uuids=[]))
        self.ble.rows["device-1"][0].name = "CSP\n\x00TEST" + "x" * 150
        result = await self.transport.dispatch({"op": "discover"})
        self.assertEqual(len(result["devices"]), 1)
        self.assertEqual(len(result["devices"][0]["name"]), 96)
        self.assertNotIn("\n", result["devices"][0]["name"])
        await self.expect_code({"op": "connect", "device_id": "foreign"}, "device_not_discovered")

    async def test_latest_discovery_replaces_ids(self):
        await self.transport.dispatch({"op": "discover"})
        self.ble.rows = {}
        await self.transport.dispatch({"op": "discover"})
        await self.expect_code({"op": "connect", "device_id": "device-1"}, "device_not_discovered")

    async def test_discovery_bounded_to64(self):
        self.ble.rows = {str(i): (NS(address=str(i), name="CSP"), NS(service_uuids=[SERVICE])) for i in range(80)}
        self.assertEqual(len((await self.transport.dispatch({"op": "discover"}))["devices"]), 64)

    async def test_connect_uses_retained_discovered_object_and_one_connection(self):
        client = await self.connect()
        self.assertIs(client.device, self.ble.device)
        await self.expect_code({"op": "connect", "device_id": "device-1"}, "transport_busy")
        await self.expect_code({"op": "discover"}, "transport_busy")
        self.assertEqual(len(self.ble.clients), 1)

    async def test_service_and_all_four_characteristics_checked_after_connect(self):
        for bad in ("service", "missing", "foreign"):
            with self.subTest(bad=bad):
                def configure(client):
                    if bad == "service":
                        client.services = NS(get_service=lambda uuid: None)
                    elif bad == "missing":
                        del client.service.characters[UUID]
                    else:
                        client.service.characters[UUID].service_uuid = "foreign"
                self.ble.configure = configure
                await self.transport.dispatch({"op": "discover"})
                await self.expect_code({"op": "connect", "device_id": "device-1"},
                                       "service_not_found" if bad == "service" else "invalid_characteristic")
                await self.transport.dispatch({"op": "disconnect"})
                self.assertEqual(self.ble.clients[-1].disconnects, 1)

    async def test_gatt_read_write_bounds_and_response_true(self):
        client = await self.connect()
        self.assertEqual(await self.transport.dispatch({"op": "read", "uuid": UUID}), {"hex": "00ff"})
        self.assertEqual(await self.transport.dispatch({"op": "write", "uuid": UUID, "hex": "ab" * 512}), {})
        self.assertEqual(client.writes, [(UUID, b"\xab" * 512, True)])
        client.read_value = b""
        self.assertEqual(await self.transport.dispatch({"op": "read", "uuid": UUID}), {"hex": ""})
        client.read_value = b"x" * 513
        await self.expect_code({"op": "read", "uuid": UUID}, "invalid_payload")
        await self.transport.dispatch({"op": "disconnect"})
        self.assertEqual(client.disconnects, 1)

    async def test_native_exception_sanitized_and_connection_retired(self):
        client = await self.connect()
        client.failure = RuntimeError("SECRET password on native OS")
        await self.expect_code({"op": "write", "uuid": UUID, "hex": "00"}, "native_operation_failed")
        await self.transport.dispatch({"op": "disconnect"})
        self.assertEqual(client.disconnects, 1)
        self.assertEqual(len(client.writes), 1)  # No ambiguous write retry.

    async def test_spontaneous_disconnect_sanitized_once(self):
        client = await self.connect()
        client.callback(client)
        client.callback(client)
        await asyncio.sleep(0)
        self.assertEqual(self.events, [{"type": "disconnected", "device_id": "device-1"}])
        await self.expect_code({"op": "read", "uuid": UUID}, "disconnected")

    async def test_stale_callback_does_not_disconnect_new_client(self):
        old = await self.connect()
        await self.transport.dispatch({"op": "disconnect"})
        new = await self.connect()
        old.callback(old)
        await asyncio.sleep(0)
        self.assertIs(self.transport._client, new)
        self.assertEqual(await self.transport.dispatch({"op": "read", "uuid": UUID}), {"hex": "00ff"})

    async def test_connect_timeout_late_success_is_disconnected_before_reuse(self):
        gate = asyncio.Event()
        def configure(client):
            client.connect_gate = gate
            client.ignore_connect_cancel = True
        self.ble.configure = configure
        await self.transport.dispatch({"op": "discover"})
        await self.expect_code({"op": "connect", "device_id": "device-1"}, "timeout")
        await self.expect_code({"op": "discover"}, "transport_busy")
        self.assertIsNone(self.transport._client)
        gate.set()
        await self.transport.dispatch({"op": "disconnect"})
        self.assertEqual(self.ble.clients[-1].disconnects, 1)
        self.assertFalse(self.ble.clients[-1].is_connected)

    async def test_cancelled_connect_cleanup_and_generation(self):
        gate = asyncio.Event()
        self.ble.configure = lambda c: setattr(c, "connect_gate", gate)
        await self.transport.dispatch({"op": "discover"})
        task = asyncio.create_task(self.transport.dispatch({"op": "connect", "device_id": "device-1"}))
        while not self.ble.clients:
            await asyncio.sleep(0)
        await self.ble.clients[-1].connect_started.wait()
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await self.transport.dispatch({"op": "disconnect"})
        self.assertEqual(self.ble.clients[-1].disconnects, 1)
        self.assertIsNone(self.transport._client)

    async def test_explicit_disconnect_cancels_pending_connect(self):
        self.ble.configure = lambda c: setattr(c, "connect_gate", asyncio.Event())
        await self.transport.dispatch({"op": "discover"})
        task = asyncio.create_task(self.transport.dispatch({"op": "connect", "device_id": "device-1"}))
        while not self.ble.clients:
            await asyncio.sleep(0)
        await self.ble.clients[-1].connect_started.wait()
        self.assertEqual(await self.transport.dispatch({"op": "disconnect"}), {})
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(self.ble.clients[-1].disconnects, 1)

    async def test_explicit_disconnect_cancels_pending_read(self):
        client = await self.connect()
        client.read_gate = asyncio.Event()
        task = asyncio.create_task(self.transport.dispatch({"op": "read", "uuid": UUID}))
        await client.read_started.wait()
        await self.transport.dispatch({"op": "disconnect"})
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(client.disconnects, 1)

    async def test_timed_out_write_not_retried_and_connection_retired(self):
        client = await self.connect()
        client.write_gate = asyncio.Event()
        await self.expect_code({"op": "write", "uuid": UUID, "hex": "00"}, "timeout")
        await self.transport.dispatch({"op": "disconnect"})
        self.assertEqual(len(client.writes), 1)
        self.assertEqual(client.disconnects, 1)

    async def test_operations_not_concurrent(self):
        client = await self.connect()
        client.read_gate = asyncio.Event()
        read = asyncio.create_task(self.transport.dispatch({"op": "read", "uuid": UUID}))
        await client.read_started.wait()
        await self.expect_code({"op": "write", "uuid": UUID, "hex": "00"}, "transport_busy")
        await self.expect_code({"op": "usb_ports"}, "transport_busy")
        client.read_gate.set()
        await read

    async def test_discovery_cancel_cannot_publish_late_inventory(self):
        self.ble.discovery_gate = asyncio.Event()
        task = asyncio.create_task(self.transport.dispatch({"op": "discover"}))
        await asyncio.sleep(0)
        await self.transport.dispatch({"op": "disconnect"})
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(self.transport._devices, {})

    async def test_cancel_suppressing_discovery_holds_native_ownership(self):
        self.ble.discovery_gate = asyncio.Event()
        self.ble.ignore_discovery_cancel = True
        task = asyncio.create_task(self.transport.dispatch({"op": "discover"}))
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await self.expect_code({"op": "discover"}, "transport_busy")
        await self.expect_code({"op": "usb_ports"}, "transport_busy")
        self.ble.discovery_gate.set()
        await self.transport.dispatch({"op": "disconnect"})
        self.assertEqual(self.transport._devices, {})

    async def test_disconnect_must_finish_before_new_connect_or_usb(self):
        client = await self.connect()
        client.disconnect_gate = asyncio.Event()
        task = asyncio.create_task(self.transport.dispatch({"op": "disconnect"}))
        while not client.disconnects:
            await asyncio.sleep(0)
        await self.expect_code({"op": "connect", "device_id": "device-1"}, "transport_busy")
        await self.expect_code({"op": "usb_ports"}, "transport_busy")
        client.disconnect_gate.set()
        self.assertEqual(await task, {})
        self.assertEqual(await self.transport.dispatch({"op": "connect", "device_id": "device-1"}), {})

    async def test_disconnect_during_read_does_not_return_late_payload(self):
        client = await self.connect()
        client.read_gate = asyncio.Event()
        task = asyncio.create_task(self.transport.dispatch({"op": "read", "uuid": UUID}))
        await client.read_started.wait()
        client.callback(client)
        await asyncio.sleep(0)
        client.read_gate.set()
        with self.assertRaises(TransportError) as caught:
            await task
        self.assertEqual(caught.exception.code, "disconnected")

    async def test_bad_request_never_reaches_either_provider(self):
        await self.expect_code({"op": "write", "uuid": UUID, "hex": "00", "pair": True}, "invalid_request")
        await self.expect_code({"op": "usb_open_setup", "port": "COM3", "command": "RESET"}, "invalid_request")
        self.assertEqual(self.ble.clients, [])
        self.assertEqual(self.usb.calls, [])

    async def test_disconnect_failure_keeps_unknown_ownership_blocked(self):
        client = await self.connect()
        async def fail():
            raise RuntimeError("SECRET")
        client.disconnect = fail
        await self.expect_code({"op": "disconnect"}, "disconnect_incomplete")
        await self.expect_code({"op": "discover"}, "transport_busy")

    async def test_usb_requires_inventory_and_uses_exact_selected_port(self):
        await self.expect_code({"op": "usb_status", "port": "COM3"}, "port_not_selected")
        self.assertEqual(await self.transport.dispatch({"op": "usb_ports"}),
                         [{"port": "COM3", "label": "ESP32-S3 USB (COM3)"}])
        self.assertEqual(await self.transport.dispatch({"op": "usb_status", "port": "COM3"}), STATUS)
        self.assertEqual(await self.transport.dispatch({"op": "usb_open_setup", "port": "COM3"}), {"accepted": True})
        self.assertEqual([row[2] for row in self.usb.calls], ["STATUS", "BENCH_WINDOW"])

    async def test_usb_reassigned_port_rejected(self):
        await self.transport.dispatch({"op": "usb_ports"})
        self.usb.listed["COM3"] = (0x303A, 0x1001, "replacement", "location")
        await self.expect_code({"op": "usb_status", "port": "COM3"}, "usb_device_changed")

    async def test_usb_timeout_retains_busy_until_native_thread_closes(self):
        await self.transport.dispatch({"op": "usb_ports"})
        self.usb.block = self.usb.ignore_stop = True
        await self.expect_code({"op": "usb_status", "port": "COM3"}, "usb_timeout")
        await self.expect_code({"op": "usb_ports"}, "transport_busy")
        await self.expect_code({"op": "discover"}, "transport_busy")
        self.usb.release.set()
        await self.transport.dispatch({"op": "disconnect"})
        self.assertIsNone(self.transport._usb_job)

    async def test_cancelled_usb_waits_for_close_before_shutdown(self):
        await self.transport.dispatch({"op": "usb_ports"})
        self.usb.block = True
        task = asyncio.create_task(self.transport.dispatch({"op": "usb_status", "port": "COM3"}))
        while not self.usb.started.is_set():
            await asyncio.sleep(.001)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await self.transport.dispatch({"op": "disconnect"})
        self.assertIsNone(self.transport._usb_job)

    async def test_cancelled_usb_cannot_release_ownership_while_thread_still_runs(self):
        await self.transport.dispatch({"op": "usb_ports"})
        self.usb.block = self.usb.ignore_stop = True
        task = asyncio.create_task(self.transport.dispatch({"op": "usb_open_setup", "port": "COM3"}))
        while not self.usb.started.is_set():
            await asyncio.sleep(.001)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await self.expect_code({"op": "usb_status", "port": "COM3"}, "transport_busy")
        await self.expect_code({"op": "disconnect"}, "disconnect_incomplete")
        self.assertIsNotNone(self.transport._usb_job)
        self.usb.release.set()
        await self.transport.dispatch({"op": "disconnect"})
        self.assertIsNone(self.transport._usb_job)
        self.assertEqual(len(self.usb.calls), 1)  # Ambiguous BENCH_WINDOW never retried.


class Stop:
    def is_set(self):
        return False

    def wait(self, duration):
        return False


class Serial:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.events = []
        self.closed = False

    def __setattr__(self, name, value):
        if name in ("dtr", "rts", "port") and "events" in self.__dict__:
            self.events.append((name, value))
        object.__setattr__(self, name, value)

    def open(self):
        self.events.append(("open", self.port))

    def reset_input_buffer(self):
        self.events.append(("drain",))

    def write(self, payload):
        self.events.append(("write", payload))
        return len(payload)

    def read(self, maximum):
        if not self.chunks:
            raise OSError("SECRET OS path")
        return self.chunks.pop(0)

    def close(self):
        self.closed = True


class NativeUSBTests(unittest.TestCase):
    def setUp(self):
        self.port = NS(device="COM3", vid=0x303A, pid=0x1001, serial_number="private", location="1-2")
        self.serial = Serial([json.dumps(dict(STATUS, ssid="NEVER-RETURN")).encode() + b"\n"])
        self.options = None
        def factory(**options):
            self.options = options
            return self.serial
        self.usb = NativeUSB(factory, lambda: [self.port], lambda port: False,
                             access_check=lambda port, mode: True)
        self.fingerprint = self.usb.ports()["COM3"]

    def exchange(self, command="STATUS"):
        return self.usb.exchange("COM3", self.fingerprint, command, Stop())

    def test_metadata_only_enumeration_filters_unrelated_or_unidentified_ports(self):
        unrelated = NS(device="COM4", vid=1, pid=2, serial_number="x", location="a")
        unknown = NS(device="COM5", vid=0x303A, pid=0x1001, serial_number=None, location=None)
        badpath = NS(device="COM3\n", vid=0x303A, pid=0x1001, serial_number="a", location="a")
        self.usb._lister = lambda: [self.port, unrelated, unknown, badpath]
        self.assertEqual(list(self.usb.ports()), ["COM3"])
        self.assertIsNone(self.options)

    def test_enumeration_disappearance_is_empty_then_fresh_inventory_recovers(self):
        # pyserial may read a disappearing sysfs idVendor as None while a USB
        # device changes modes. A failed snapshot must grant no stale identity.
        from unittest.mock import Mock
        from cloudstick_desktop import firmware_usb
        for error in (TypeError("disappeared sysfs value"), OSError("removed device")):
            with self.subTest(error=type(error).__name__):
                self.usb._lister = Mock(side_effect=[error, [self.port]])
                self.assertEqual(self.usb.ports(), {})
                self.assertEqual(self.usb.ports(), {"COM3": self.fingerprint})
                with patch('serial.tools.list_ports.comports', side_effect=[error, [self.port]]):
                    self.assertEqual(firmware_usb.ports(), {})
                    self.assertEqual(firmware_usb.ports(), {"COM3": self.fingerprint})
        self.assertIsNone(self.options)  # enumeration never opens a serial port

    def test_unexpected_enumerator_failure_is_not_hidden(self):
        from unittest.mock import Mock
        from cloudstick_desktop import firmware_usb
        self.usb._lister = Mock(side_effect=RuntimeError("unexpected provider failure"))
        with self.assertRaises(RuntimeError):
            self.usb.ports()
        with patch('serial.tools.list_ports.comports', side_effect=RuntimeError("unexpected provider failure")):
            with self.assertRaises(RuntimeError):
                firmware_usb.ports()

    def test_actual_serial_command_and_nonreset_line_sequence(self):
        self.assertEqual(self.exchange(), STATUS)
        self.assertEqual(self.options["baudrate"], 115200)
        self.assertIsNone(self.options["port"])
        self.assertEqual(self.serial.events[:4], [("dtr", False), ("rts", False), ("port", "COM3"), ("open", "COM3")])
        self.assertEqual(self.serial.events[4:], [("dtr", True), ("drain",), ("write", b"STATUS\n")])
        self.assertTrue(self.serial.closed)

    def test_bench_returns_only_typed_acceptance(self):
        self.serial.chunks = [b'{"bench_window":true,"ssid":"NEVER-RETURN"}\n']
        self.assertEqual(self.exchange("BENCH_WINDOW"), {"accepted": True})
        self.assertIn(("write", b"BENCH_WINDOW\n"), self.serial.events)

    def test_busy_and_changed_identity_never_open_or_write(self):
        self.usb._busy_check = lambda port: True
        with self.assertRaises(TransportError):
            self.exchange()
        self.assertIsNone(self.options)
        self.usb._busy_check = lambda port: False
        self.port.serial_number = "different"
        with self.assertRaises(TransportError):
            self.exchange()
        self.assertIsNone(self.options)

    def test_reidentify_after_open_before_command(self):
        original = self.serial.open
        def change():
            original()
            self.port.serial_number = "different"
        self.serial.open = change
        with self.assertRaises(TransportError):
            self.exchange()
        self.assertFalse(any(row[0] == "write" for row in self.serial.events))
        self.assertTrue(self.serial.closed)

    def test_invalid_or_ambiguous_responses_close_without_returning_raw_data(self):
        for response in [b'{"bench_window":1}\n', b'{"bench_window":true,"bench_window":false}\n',
                         b'{"unknown":"SECRET"}\n', b'{broken}\n', b'{"bench_window":false}\n' + b'x' * 8193]:
            with self.subTest(response=response[:40]):
                self.serial = Serial([response])
                with self.assertRaises(TransportError):
                    self.exchange("BENCH_WINDOW")
                self.assertTrue(self.serial.closed)

    def test_split_lines_and_bounded_nonjson_startup_noise(self):
        encoded = json.dumps(STATUS).encode()
        self.serial.chunks = [b"startup\r\n" + encoded[:20], encoded[20:] + b"\n{\"wifi_step\":0}\n"]
        self.assertEqual(self.exchange(), STATUS)

    def test_only_two_commands_allowed_before_serial_creation(self):
        for command in ("CHECK", "TEST", "SETUP", "OWNER_INSTALL", "STATUS\nBENCH_WINDOW"):
            with self.subTest(command=command), self.assertRaises(TransportError):
                self.exchange(command)
        self.assertIsNone(self.options)

    def test_native_read_failure_always_closes(self):
        self.serial.chunks = []
        with self.assertRaises(OSError):
            self.exchange()
        self.assertTrue(self.serial.closed)

    def test_unknown_busy_check_refuses_before_open(self):
        def unknown(port):
            raise TransportError("usb_busy_unknown")
        self.usb._busy_check = unknown
        with self.assertRaises(TransportError):
            self.exchange()
        self.assertIsNone(self.options)

    def test_permission_denied_precedes_busy_check_and_never_opens(self):
        calls = []
        self.usb._access_check = lambda port, mode: calls.append((port, mode)) or False
        def must_not_probe(port):
            self.fail("Ownership must not be probed before device access is allowed")
        self.usb._busy_check = must_not_probe
        with self.assertRaises(TransportError) as caught:
            self.exchange()
        self.assertEqual(caught.exception.code, "usb_permission_denied")
        self.assertEqual(calls, [("COM3", os.R_OK | os.W_OK)])
        self.assertIsNone(self.options)

    def test_permission_grant_does_not_bypass_unknown_ownership(self):
        self.usb._access_check = lambda port, mode: True
        def unknown(port):
            raise TransportError("usb_busy_unknown")
        self.usb._busy_check = unknown
        with self.assertRaises(TransportError) as caught:
            self.exchange()
        self.assertEqual(caught.exception.code, "usb_busy_unknown")
        self.assertIsNone(self.options)

    def test_fuser_uses_discovered_executable_and_noble_compatible_argv(self):
        with patch("cloudstick_desktop.transports.os.name", "posix"), \
                patch("cloudstick_desktop.transports.shutil.which", return_value="/usr/bin/fuser"), \
                patch("cloudstick_desktop.transports.subprocess.run", return_value=NS(returncode=1, stdout=b"", stderr=b"")) as run:
            self.assertFalse(NativeUSB._busy("/dev/ttyACM0"))
        run.assert_called_once_with(["/usr/bin/fuser", "/dev/ttyACM0"], capture_output=True, timeout=2, check=False)

    def test_fuser_unexpected_stderr_or_output_still_fails_closed(self):
        for code, output, error in [(1, b"", b"No process specification given\nUsage: fuser"),
                                    (1, b"1234", b""), (2, b"", b"")]:
            with self.subTest(code=code, error=error), \
                    patch("cloudstick_desktop.transports.os.name", "posix"), \
                    patch("cloudstick_desktop.transports.shutil.which", return_value="/usr/bin/fuser"), \
                    patch("cloudstick_desktop.transports.subprocess.run", return_value=NS(returncode=code, stdout=output, stderr=error)):
                with self.assertRaises(TransportError) as caught:
                    NativeUSB._busy("/dev/ttyACM0")
                self.assertEqual(caught.exception.code, "usb_busy_unknown")

    def test_fuser_refuses_nonabsolute_option_or_unbounded_arguments(self):
        for port in ("--", "ttyACM0", "COM3", "/dev/ttyACM0\n", "/dev/ttyACM0 extra", "/dev/ttyACM12345"):
            with self.subTest(port=port), \
                    patch("cloudstick_desktop.transports.os.name", "posix"), \
                    patch("cloudstick_desktop.transports.subprocess.run") as run:
                with self.assertRaises(TransportError):
                    NativeUSB._busy(port)
                run.assert_not_called()

    def test_fuser_real_holder_remains_busy(self):
        with patch("cloudstick_desktop.transports.os.name", "posix"), \
                patch("cloudstick_desktop.transports.shutil.which", return_value="/usr/bin/fuser"), \
                patch("cloudstick_desktop.transports.subprocess.run", return_value=NS(returncode=0, stdout=b"1234", stderr=b"/dev/ttyACM0:")):
            self.assertTrue(NativeUSB._busy("/dev/ttyACM0"))

    def test_short_serial_write_closes_without_read_or_retry(self):
        self.serial.write = lambda payload: len(payload) - 1
        with self.assertRaises(TransportError) as caught:
            self.exchange()
        self.assertEqual(caught.exception.code, "usb_write_failed")
        self.assertTrue(self.serial.closed)
        self.assertEqual(len(self.serial.chunks), 1)

    def test_stop_before_open_never_creates_serial(self):
        stop = threading.Event()
        stop.set()
        with self.assertRaises(TransportError):
            self.usb.exchange("COM3", self.fingerprint, "STATUS", stop)
        self.assertIsNone(self.options)

    def test_partial_json_at_timeout_closes(self):
        self.serial.chunks = [b'{"firmware":']
        with patch("cloudstick_desktop.transports.time.monotonic", side_effect=[0, 0, 7]):
            with self.assertRaises(TransportError) as caught:
                self.exchange()
        self.assertEqual(caught.exception.code, "usb_timeout")
        self.assertTrue(self.serial.closed)


if __name__ == "__main__":
    unittest.main()
