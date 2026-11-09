"""User-triggered native BLE and USB diagnostics, with injectable OS providers.

Normal setup transport. The separately guarded firmware manager owns its worker. USB opens
only the selected, freshly reidentified ESP32-S3 CDC port. The two commands here
do not implement USB settings: BENCH_WINDOW requests the existing BLE window;
the JS Security2 handshake still authorizes all settings.
"""

import asyncio
import json
import os
import re
import shutil
import subprocess
import threading
import time
from contextlib import contextmanager

from .protocol import (CHARACTERISTICS, MAX_BYTES, SERVICE, TransportError,
                       diagnostic_status, validate_request)
from .setup_access import SetupAccess
from .enrollment import OwnerChannel, validate_record, REVIEWED_FIRMWARE
from .diagnostics import DiagnosticsChannel


class NativeBLE:
    """Lazy Bleak import: construction and offline validation touch no radio."""

    async def discover(self, timeout):
        from bleak import BleakScanner
        return await BleakScanner.discover(timeout=timeout, service_uuids=[SERVICE], return_adv=True)

    def client(self, device, disconnected_callback):
        from bleak import BleakClient
        return BleakClient(device, disconnected_callback=disconnected_callback, timeout=10, pair=False)


def _port_fingerprint(port):
    # Hardware identity remains in RAM; it is never returned to JS or logged.
    return tuple(getattr(port, key, None) for key in ("vid", "pid", "serial_number", "location"))


class NativeUSB:
    """Blocking pyserial implementation; DesktopTransport runs it off the GUI."""

    def __init__(self, serial_factory=None, port_lister=None, busy_check=None, access_check=None,
                 *, owner_clock=None, owner_wait=None):
        self._reviewed_firmware = REVIEWED_FIRMWARE
        self._factory = serial_factory
        self._lister = port_lister
        self._busy_check = busy_check or self._busy
        self._access_check = access_check or os.access
        self._owner_uncertain = set()
        self._retained_session = None
        self._owner_clock, self._owner_wait = owner_clock, owner_wait

    def admit_catalog(self, raw):
        # Only an actual release-key signature admits later versions of the
        # same v1 management contract. Local review/renderer flags grant none.
        from .firmware_catalog import load_catalog
        catalog = load_catalog(raw)
        self._reviewed_firmware = REVIEWED_FIRMWARE | frozenset(app.version for app in catalog.known_apps)

    def ports(self):
        if self._lister is None:
            from serial.tools.list_ports import comports
            rows = comports()
        else:
            rows = self._lister()
        # Do not send even STATUS to unrelated serial equipment. ESP32-S3's
        # reviewed native CDC VID/PID is 303a:1001, not a general port matcher.
        result = {}
        for port in rows:
            name = getattr(port, "device", "")
            fingerprint = _port_fingerprint(port)
            if (fingerprint[:2] == (0x303A, 0x1001) and (fingerprint[2] or fingerprint[3])
                    and type(name) is str and re.fullmatch(r"COM[1-9][0-9]{0,3}|/dev/tty(?:ACM|USB)[0-9]{1,4}", name)):
                result[name] = fingerprint
            if len(result) >= 64:
                break
        return result

    @staticmethod
    def _busy(port):
        # POSIX exclusive flock protects cooperating clients; fuser detects
        # other owners where installed. Windows CreateFile is exclusive itself.
        # Neither mechanism makes arbitrary third-party serial programs safe.
        if os.name != "posix":
            return False
        # Noble's fuser rejects "--". A strictly bounded absolute tty path is
        # already a non-option argument; never pass arbitrary caller text here.
        if type(port) is not str or not re.fullmatch(r"/dev/tty(?:ACM|USB)[0-9]{1,4}", port):
            raise TransportError("invalid_port")
        executable = shutil.which("fuser")
        if not executable:
            return False
        try:
            result = subprocess.run([executable, port], capture_output=True, timeout=2, check=False)
        except (OSError, subprocess.TimeoutExpired):
            raise TransportError("usb_busy_unknown") from None
        if result.returncode == 0:
            return True
        if result.returncode != 1 or result.stdout.strip() or result.stderr.strip():
            raise TransportError("usb_busy_unknown")
        return False

    @staticmethod
    def _json(line):
        def unique(pairs):
            row = {}
            for key, value in pairs:
                if key in row:
                    raise ValueError()
                row[key] = value
            return row
        try:
            row = json.loads(line, object_pairs_hook=unique)
        except (ValueError, UnicodeError, RecursionError):
            raise TransportError("invalid_usb_response") from None
        if type(row) is not dict:
            raise TransportError("invalid_usb_response")
        return row

    @contextmanager
    def _session(self, port, fingerprint, stop):
        if self._retained_session is not None:
            raise TransportError("usb_cleanup_unknown")
        if self.ports().get(port) != fingerprint:
            raise TransportError("usb_device_changed")
        # A missing device ACL is not evidence that ownership is safe.
        if not self._access_check(port, os.R_OK | os.W_OK):
            raise TransportError("usb_permission_denied")
        if self._busy_check(port):
            raise TransportError("usb_busy")
        if stop.is_set():
            raise TransportError("cancelled")
        factory = self._factory
        if factory is None:
            import serial
            factory = serial.Serial
        options = dict(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
        if os.name == "posix":
            options["exclusive"] = True
        serial_port = factory(**options)
        try:
            # Same non-reset convention as reviewed tools/device_status.py.
            # TinyUSB TX requires DTR. RTS never asserts; never use1200 baud,
            # esptool, send_break, reset commands or a DTR/RTS reset sequence.
            serial_port.dtr = False
            serial_port.rts = False
            serial_port.port = port
            serial_port.open()
            serial_port.dtr = True
            if stop.wait(0.3):
                raise TransportError("cancelled")
            # Reidentify after open as well; a reused path must not be trusted.
            if self.ports().get(port) != fingerprint:
                raise TransportError("usb_device_changed")
            serial_port.reset_input_buffer()
            yield serial_port
        finally:
            try:
                serial_port.close()
            except BaseException:
                # Retain the provider object and block another open when native
                # descriptor ownership is uncertain. Never reset to recover it.
                self._retained_session = serial_port
                raise TransportError("usb_cleanup_unknown") from None

    def exchange(self, port, fingerprint, command, stop):
        if command not in ("STATUS", "BENCH_WINDOW", "DASHBOARD_STATUS"):
            raise TransportError("invalid_request")
        with self._session(port, fingerprint, stop) as serial_port:
            payload = command.encode("ascii") + b"\n"
            if serial_port.write(payload) != len(payload):
                raise TransportError("usb_write_failed")
            deadline = time.monotonic() + 6
            received = bytearray()
            total = 0
            while time.monotonic() < deadline and not stop.is_set():
                chunk = serial_port.read(1024)
                total += len(chunk)
                received.extend(chunk)
                if total > 16384 or len(received) > 8192:
                    raise TransportError("usb_response_too_large")
                while b"\n" in received:
                    line, _, remainder = received.partition(b"\n")
                    received = bytearray(remainder)
                    if not line.startswith(b"{"):
                        continue
                    row = self._json(line)
                    if command == 'DASHBOARD_STATUS':
                        from .diagnostics import dashboard_row
                        return dashboard_row(row)
                    if command == "STATUS" and "firmware" in row:
                        return diagnostic_status(row)
                    if command == "BENCH_WINDOW" and "bench_window" in row:
                        if type(row["bench_window"]) is not bool:
                            raise TransportError("invalid_usb_response")
                        return {"accepted": row["bench_window"]}
                    # A complete unrelated JSON response is ambiguous, not a
                    # reason to accept a later response to a different request.
                    raise TransportError("invalid_usb_response")
            raise TransportError("cancelled" if stop.is_set() else "usb_timeout")

    def owner_exchange(self, port, fingerprint, op, stop, record_hex=None):
        if op not in ("usb_owner_status", "usb_owner_identity", "usb_owner_install"):
            raise TransportError("invalid_request")
        if (type(fingerprint) is not tuple or len(fingerprint) != 4
                or fingerprint[:2] != (0x303A, 0x1001)
                or type(fingerprint[2]) is not str or not fingerprint[2].strip()):
            raise TransportError("owner_identity_unavailable")
        if op == "usb_owner_install":
            validate_record(record_hex)
            if fingerprint in self._owner_uncertain:
                raise TransportError("owner_install_unknown")
        channel = None
        try:
            with self._session(port, fingerprint, stop) as serial_port:
                channel = OwnerChannel(serial_port, stop, lambda: self.ports().get(port) == fingerprint, self._json,
                                       clock=self._owner_clock, wait=self._owner_wait, reviewed_firmware=self._reviewed_firmware)
                return channel.run(op, record_hex)
        except BaseException:
            if channel is not None and channel.attempted:
                self._owner_uncertain.add(fingerprint)
                raise TransportError("owner_install_unknown") from None
            raise

    def diagnostics(self, port, fingerprint, stop):
        if (type(fingerprint) is not tuple or len(fingerprint) != 4
                or fingerprint[:2] != (0x303A, 0x1001)
                or type(fingerprint[2]) is not str or not fingerprint[2].strip()):
            raise TransportError("diagnostics_identity_unavailable")
        with self._session(port, fingerprint, stop) as serial_port:
            channel = DiagnosticsChannel(serial_port, stop, lambda: self.ports().get(port) == fingerprint, self._json,
                                         clock=self._owner_clock, wait=self._owner_wait, reviewed_firmware=self._reviewed_firmware)
            result = channel.run()
        if stop.is_set():
            raise TransportError("cancelled")
        if self.ports().get(port) != fingerprint:
            raise TransportError("usb_device_changed")
        return result


class DesktopTransport:
    def __init__(self, event_sink=None, *, ble=None, usb=None, discovery_timeout=5,
                 operation_timeout=12, cleanup_timeout=5, setup_access=None):
        self._ble = ble or NativeBLE()
        self._usb = usb or NativeUSB()
        self._setup_access = setup_access or SetupAccess()
        self._setup_device_id = None
        self._event_sink = event_sink or (lambda event: None)
        self._discovery_timeout = discovery_timeout
        self._timeout = operation_timeout
        self._cleanup_timeout = cleanup_timeout
        self._generation = 0
        self._devices = {}
        self._client = None
        self._device_id = None
        self._connecting = None
        self._ble_busy = False
        self._active_ble = None
        self._disconnecting = False
        self._usb_job = None
        self._usb_stop = None
        self._ports = {}
        self._retiring = set()
        self._background = set()
        self._firmware = None
        self._firmware_starting = False

    def _track(self, coroutine):
        task = asyncio.create_task(coroutine)
        self._background.add(task)
        def done(completed):
            self._background.discard(completed)
            if not completed.cancelled():
                completed.exception()  # Consume OS errors without logging data.
        task.add_done_callback(done)
        return task

    async def _bounded(self, awaitable, timeout=None, on_abandon=None):
        task = asyncio.ensure_future(awaitable)
        try:
            done, _ = await asyncio.wait({task}, timeout=self._timeout if timeout is None else timeout)
            if not done:
                raise TransportError("timeout")
            return task.result()
        except BaseException:
            if not task.done():
                task.cancel()
            if on_abandon:
                on_abandon(task)
            else:
                # Providers are permitted to finish cancellation asynchronously.
                marker = object()
                self._retiring.add(marker)
                async def drain():
                    await self._drain(task)
                    self._retiring.discard(marker)
                self._track(drain())
            raise

    @staticmethod
    async def _drain(task):
        try:
            await task
        except BaseException:
            pass

    def _emit(self, device_id):
        event = {"type": "disconnected"}
        if device_id:
            event["device_id"] = device_id
        try:
            self._event_sink(event)
        except Exception:
            pass  # UI consumers never prevent connection retirement.

    def _lost(self, generation, client):
        if generation != self._generation or client is not self._client:
            return
        identifier = self._device_id
        self._client = None
        self._device_id = None
        self._generation += 1
        self._emit(identifier)

    def _retire(self, client, pending=None):
        # Keep ownership until a timed-out/late connect or GATT operation has
        # settled and disconnect has completed. A new connect cannot overlap.
        marker = object()
        self._retiring.add(marker)
        async def cleanup():
            try:
                if pending is not None:
                    await self._drain(pending)
                await client.disconnect()
            except BaseException:
                # Ambiguous native ownership remains blocked for this process.
                return
            self._retiring.discard(marker)
        return self._track(cleanup())

    def _abandon(self, generation, client, pending=None):
        if self._generation == generation:
            self._generation += 1
            identifier = self._device_id
            self._client = None
            self._device_id = None
            self._emit(identifier)
        self._retire(client, pending)

    @property
    def firmware(self):
        if self._firmware is None:
            from .firmware_manager import FirmwareManager
            self._firmware = FirmwareManager(self._usb)
        return self._firmware

    def _firmware_idle_transport(self):
        if self._client is not None or self._ble_busy or self._retiring or self._usb_job is not None or self._disconnecting or self._firmware_starting:
            raise TransportError('transport_busy')

    async def start_firmware(self, request, approval):
        validate_request(request)
        self._firmware_idle_transport()
        self._firmware_starting = True
        try:
            return await asyncio.to_thread(self.firmware.start, request, approval)
        finally:
            self._firmware_starting = False

    async def wait_firmware(self):
        if self._firmware is not None:
            await self._firmware.wait()

    async def dispatch(self, request):
        op = validate_request(request)
        try:
            if op in ('lan_discover', 'lan_status'):
                self._firmware_idle_transport()
                if self._firmware is not None and self._firmware.blocked:
                    raise TransportError('firmware_busy')
                from . import lan
                return await asyncio.to_thread(lan.discover if op == 'lan_discover' else lambda: lan.status(request['address']))
            if op.startswith('app_update_'):
                if not hasattr(self, '_app_updates'):
                    from .app_updates import AppUpdates
                    self._app_updates = AppUpdates()
                if op == 'app_update_state':
                    return self._app_updates.state()
                self._firmware_idle_transport()
                if self._firmware is not None and self._firmware.blocked:
                    raise TransportError('firmware_busy')
                return self._app_updates.start(download=op == 'app_update_download')
            if op.startswith('firmware_'):
                if op == 'firmware_state':
                    return self.firmware.state()
                if op == 'firmware_start':
                    raise TransportError('firmware_native_approval_required')
                self._firmware_idle_transport()
                if op == 'firmware_ports':
                    return await asyncio.to_thread(self.firmware.ports)
                if op == 'firmware_check':
                    return self.firmware.check()
                if op == 'firmware_prepare':
                    return self.firmware.prepare(request['port'])
            if (self._firmware_starting or (self._firmware is not None and self._firmware.blocked)) and op != 'disconnect':
                raise TransportError('firmware_busy')
            if op == "disconnect":
                return await self._disconnect()
            if self._disconnecting:
                raise TransportError("transport_busy")
            if op.startswith("usb_"):
                return await self._usb_dispatch(op, request)
            if self._usb_job is not None or self._ble_busy or self._retiring:
                raise TransportError("transport_busy")
            if op == "discover" and self._client is not None:
                raise TransportError("transport_busy")
            self._ble_busy = True
            self._active_ble = asyncio.current_task()
            try:
                if op == "discover":
                    return await self._discover()
                if op == "connect":
                    return await self._connect(request["device_id"])
                return await self._gatt(op, request)
            finally:
                self._ble_busy = False
                self._active_ble = None
        except TransportError:
            raise
        except asyncio.CancelledError:
            self._setup_device_id = None
            raise
        except Exception:
            raise TransportError("native_operation_failed") from None

    async def _discover(self):
        generation = self._generation
        self._devices = {}
        rows = await self._bounded(self._ble.discover(self._discovery_timeout), self._discovery_timeout + 2)
        if generation != self._generation:
            raise TransportError("cancelled")
        devices = {}
        if type(rows) is not dict:
            raise TransportError("invalid_discovery")
        for device, advertisement in rows.values():
            identifier = getattr(device, "address", None)
            services = getattr(advertisement, "service_uuids", None) or []
            if (type(identifier) is not str or not 1 <= len(identifier) <= 256
                    or any(ord(c) < 32 or ord(c) == 127 for c in identifier)
                    or SERVICE not in [str(uuid).lower() for uuid in services]):
                continue
            name = getattr(advertisement, "local_name", None) or getattr(device, "name", None) or "Cloud Stick"
            name = "".join(c for c in str(name) if ord(c) >= 32 and ord(c) != 127)[:96]
            devices[identifier] = (device, name)
            if len(devices) >= 64:
                break
        self._devices = devices
        return {"devices": [{"id": key, "name": value[1]} for key, value in devices.items()]}

    async def _connect(self, identifier):
        if self._client is not None or self._connecting is not None:
            raise TransportError("transport_busy")
        if identifier not in self._devices:
            raise TransportError("device_not_discovered")
        if self._setup_device_id is not None and identifier != self._setup_device_id:
            raise TransportError("setup_access_device_mismatch")
        self._generation += 1
        generation = self._generation
        loop = asyncio.get_running_loop()
        def disconnected(client):
            try:
                loop.call_soon_threadsafe(self._lost, generation, client)
            except RuntimeError:
                pass  # The application event loop has already closed.
        client = self._ble.client(self._devices[identifier][0], disconnected)
        self._client, self._device_id = client, identifier
        self._connecting = client
        abandoned = False
        def abandon(task):
            nonlocal abandoned
            abandoned = True
            self._abandon(generation, client, task)
        try:
            await self._bounded(client.connect(), on_abandon=abandon)
            if generation != self._generation or self._client is not client:
                raise TransportError("cancelled")
            if not client.is_connected or client.services.get_service(SERVICE) is None:
                raise TransportError("service_not_found")
            for uuid in CHARACTERISTICS:
                self._characteristic(client, uuid)
            return {}
        except BaseException:
            if not abandoned:
                self._abandon(generation, client)
            raise
        finally:
            self._setup_device_id = None
            if self._connecting is client:
                self._connecting = None

    @staticmethod
    def _characteristic(client, uuid):
        service = client.services.get_service(SERVICE)
        if service is None:
            raise TransportError("service_not_found")
        characteristic = service.get_characteristic(uuid)
        if characteristic is None or str(characteristic.service_uuid).lower() != SERVICE:
            raise TransportError("invalid_characteristic")
        return characteristic

    async def _gatt(self, op, request):
        client, generation = self._client, self._generation
        if client is None or not client.is_connected:
            raise TransportError("disconnected")
        characteristic = self._characteristic(client, request["uuid"])
        operation = (client.read_gatt_char(characteristic) if op == "read" else
                     client.write_gatt_char(characteristic, bytes.fromhex(request["hex"]), response=True))
        result = await self._bounded(operation, on_abandon=lambda task: self._abandon(generation, client, task))
        if generation != self._generation or self._client is not client:
            raise TransportError("disconnected")
        if op == "read":
            if not isinstance(result, (bytes, bytearray)) or len(result) > MAX_BYTES:
                self._abandon(generation, client)
                raise TransportError("invalid_payload")
            return {"hex": bytes(result).hex()}
        return {}

    async def _disconnect(self):
        if self._disconnecting:
            raise TransportError("transport_busy")
        self._disconnecting = True
        try:
            return await self._disconnect_owned()
        finally:
            self._disconnecting = False

    async def _disconnect_owned(self):
        self._setup_device_id = None
        self._generation += 1
        client, identifier = self._client, self._device_id
        self._client = self._device_id = None
        if client is not None:
            self._emit(identifier)
        active = self._active_ble
        if active is not None:
            active.cancel()
            done, _ = await asyncio.wait({active}, timeout=self._cleanup_timeout)
            if not done:
                raise TransportError("disconnect_incomplete")
        elif client is not None:
            self._retire(client)
        if self._usb_stop is not None:
            self._usb_stop.set()
        if self._usb_job is not None:
            done, _ = await asyncio.wait({self._usb_job}, timeout=self._cleanup_timeout)
            if not done:
                raise TransportError("disconnect_incomplete")
        if self._background:
            await asyncio.wait(set(self._background), timeout=self._cleanup_timeout)
        if self._retiring:
            raise TransportError("disconnect_incomplete")
        return {}

    async def _usb_dispatch(self, op, request):
        if self._client is not None or self._ble_busy or self._retiring or self._usb_job is not None:
            raise TransportError("transport_busy")
        stop = threading.Event()
        generation = self._generation
        self._usb_stop = stop
        if op in ("usb_ports", "usb_watch_ports"):
            call = self._usb.ports
        else:
            port = request["port"]
            if port not in self._ports:
                raise TransportError("port_not_selected")
            fingerprint = self._ports[port]
            if op == "usb_diagnostics":
                call = lambda: self._usb.diagnostics(port, fingerprint, stop)
            elif op in ("usb_owner_status", "usb_owner_identity", "usb_owner_install"):
                call = lambda: self._usb.owner_exchange(port, fingerprint, op, stop, request.get("record_hex"))
            elif op in ("usb_setup_access_status", "usb_setup_access", "usb_forget_setup_access"):
                def call():
                    if stop.is_set():
                        raise TransportError("cancelled")
                    if self._usb.ports().get(port) != fingerprint:
                        raise TransportError("usb_device_changed")
                    if op == "usb_setup_access_status":
                        value = {"available": self._setup_access.available(fingerprint)}
                    elif op == "usb_forget_setup_access":
                        value = self._setup_access.forget(fingerprint)
                    else:
                        value = self._setup_access.load(fingerprint)
                    if self._usb.ports().get(port) != fingerprint:
                        raise TransportError("usb_device_changed")
                    if stop.is_set():
                        raise TransportError("cancelled")
                    return value
            else:
                command = 'DASHBOARD_STATUS' if op == 'usb_address' else "STATUS" if op == "usb_status" else "BENCH_WINDOW"
                call = lambda: self._usb.exchange(port, fingerprint, command, stop)
        task = asyncio.create_task(asyncio.to_thread(call))
        self._usb_job = task
        def finished(completed):
            if self._usb_job is completed:
                self._usb_job = self._usb_stop = None
            if not completed.cancelled():
                completed.exception()
        task.add_done_callback(finished)
        try:
            result = await asyncio.wait_for(asyncio.shield(task), timeout=self._timeout)
            if op in ("usb_ports", "usb_watch_ports"):
                self._ports = result
                if op == 'usb_watch_ports':
                    import secrets
                    previous = getattr(self, '_watched_ports', {})
                    watched = {port: (fingerprint, previous[port][1] if port in previous and previous[port][0] == fingerprint else secrets.token_hex(8)) for port, fingerprint in result.items()}
                    self._watched_ports = watched
                    return [{'port': port, 'label': 'ESP32-S3 USB (' + port + ')', 'token': watched[port][1]} for port in result]
                return [{"port": port, "label": "ESP32-S3 USB (" + port + ")"} for port in result]
            if op in ("usb_setup_access_status", "usb_setup_access", "usb_forget_setup_access") and (stop.is_set() or generation != self._generation):
                raise TransportError("cancelled")
            if op in ("usb_owner_status", "usb_owner_identity", "usb_owner_install") and (stop.is_set() or generation != self._generation):
                raise TransportError("owner_install_unknown" if op == "usb_owner_install" else "cancelled")
            if op == "usb_diagnostics" and (stop.is_set() or generation != self._generation):
                raise TransportError("cancelled")
            if op == "usb_owner_identity":
                mac = result["device"].upper()
                self._setup_device_id = ":".join(mac[n:n + 2] for n in range(0, 12, 2))
            elif op == "usb_owner_install":
                mac = validate_record(request["record_hex"]).upper()
                self._setup_device_id = ":".join(mac[n:n + 2] for n in range(0, 12, 2))
            if op == "usb_setup_access":
                self._setup_device_id = result["device_id"]
            elif op == "usb_forget_setup_access":
                self._setup_device_id = None
            return result
        except asyncio.TimeoutError:
            stop.set()
            if op == "usb_owner_install":
                raise TransportError("owner_install_unknown") from None
            raise TransportError("usb_timeout") from None
        except asyncio.CancelledError:
            stop.set()
            if op == "usb_owner_install":
                raise TransportError("owner_install_unknown") from None
            raise
