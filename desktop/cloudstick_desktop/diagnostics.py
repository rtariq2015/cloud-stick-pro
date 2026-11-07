"""Read-only, name-free diagnostics for the reviewed firmware serial protocol.

This is a sequential snapshot, not an atomic health proof. Configured flags do
not verify internet/cloud access; a serial reply does not verify USB MSC or SD
files. There are no reset, eject, flash, settings or cloud-test commands here.
"""
import ipaddress
import time

from .enrollment import REVIEWED_FIRMWARE
from .protocol import TransportError, diagnostic_status


U32 = (0, 2**32 - 1)
I32 = (-2**31, 2**31 - 1)
U64 = (0, 2**64 - 1)
STATUS_FIELDS = frozenset({"firmware", "setup_only", "owner_ready", "store_fault", "ble", "wifi",
                           "saved_revision", "internet_verified", "backups_paused", "internal_free", "largest_block"})
LIFECYCLE_BOOLS = ("ble_recovery_checked", "ble_memory_restored")
LIFECYCLE_INTS = ("ble_before_free", "ble_before_largest", "ble_after_free", "ble_after_largest",
                  "save_before_free", "save_before_largest", "save_after_free", "save_after_largest",
                  "internal_minimum", "loop_stack_free", "host_exit_stack_min")
MEMORY_BOOLS = ("failure_hook_registered", "allocation_failure_seen")
MEMORY_INTS = ("samples", "observed_min_free", "observed_min_largest")
BACKUP_BOOLS = ("cloud_configured", "paused", "active", "has_scan", "scan_complete")
BACKUP_INTS = {
    "worker_phase": (0, 5), "worker_generation": U32, "worker_error": I32,
    "verified": U32, "pending": U32, "failed": U32,
    "last_backup_unix": (0, 253402300799), "error": I32, "phase": (0, 4),
    "done": U64, "total": U64, "speed": U32, "worker_stack_free": U32, "tls_minimum_heap": U32,
}


def typed(row, *, booleans=(), integers=None):
    integers = integers or {}
    if type(row) is not dict or set(row) != set(booleans) | set(integers):
        raise TransportError("invalid_diagnostics_response")
    for key in booleans:
        if type(row[key]) is not bool:
            raise TransportError("invalid_diagnostics_response")
    for key, (low, high) in integers.items():
        if type(row[key]) is not int or not low <= row[key] <= high:
            raise TransportError("invalid_diagnostics_response")
    return dict(row)


def status_rows(rows, *, reviewed_firmware=REVIEWED_FIRMWARE):
    if type(rows) is not list or len(rows) != 3 or type(rows[0]) is not dict or set(rows[0]) != STATUS_FIELDS:
        raise TransportError("invalid_diagnostics_response")
    status = diagnostic_status(rows[0])
    if status["firmware"] not in reviewed_firmware:
        raise TransportError("diagnostics_firmware_unsupported")
    # This exact reviewed wire flag is a constant false, not a live probe result.
    if status["internet_verified"] is not False:
        raise TransportError("invalid_diagnostics_response")
    radio = typed(rows[1], integers={"wifi_step": (0, 28), "wifi_sdk_error": I32, "wifi_error": (0, 20)})
    lifecycle = typed(rows[2], booleans=LIFECYCLE_BOOLS, integers={name: U32 for name in LIFECYCLE_INTS})
    return status, radio, lifecycle


def dashboard_row(row):
    if type(row) is not dict or set(row) != {"dashboard_listening", "dashboard_fault", "station_ip"}:
        raise TransportError("invalid_diagnostics_response")
    flags = typed({key: row[key] for key in ("dashboard_listening", "dashboard_fault")},
                  booleans=("dashboard_listening", "dashboard_fault"))
    address = row["station_ip"]
    if type(address) is not str or len(address) > 15:
        raise TransportError("invalid_diagnostics_response")
    try:
        if str(ipaddress.IPv4Address(address)) != address:
            raise ValueError()
    except ipaddress.AddressValueError:
        raise TransportError("invalid_diagnostics_response") from None
    except ValueError:
        raise TransportError("invalid_diagnostics_response") from None
    return dict(flags, station_ip=address)


class DiagnosticsChannel:
    """Fixed read-only command set on one already-open exclusive USB session.

    The first send and every later send wait350ms. Later pacing starts only
    after all expected response rows arrive, covering firmware's250ms limiter.
    Clock/wait injection is for synthetic serial fixtures, never renderer input.
    """
    COMMANDS = frozenset({"STATUS", "BOOT_STATUS", "MEMORY_STATUS", "BACKUP_STATUS", "DASHBOARD_STATUS"})

    def __init__(self, serial_port, stop, same_device, json_parser, *, clock=None, wait=None, reviewed_firmware=None):
        self.reviewed_firmware = REVIEWED_FIRMWARE if reviewed_firmware is None else frozenset(reviewed_firmware)
        self.serial, self.stop = serial_port, stop
        self.same_device, self.json_parser = same_device, json_parser
        self.clock, self.wait = clock or time.monotonic, wait or stop.wait
        self.next_send = self.clock() + .35
        self.pending = bytearray()

    def check(self):
        if self.stop.is_set():
            raise TransportError("cancelled")
        if not self.same_device():
            raise TransportError("usb_device_changed")

    def request(self, command):
        if command not in self.COMMANDS:
            raise TransportError("invalid_request")
        self.check()
        while (remaining := self.next_send - self.clock()) > 0:
            if self.wait(min(remaining, .05)):
                raise TransportError("cancelled")
            self.check()
        self.check()
        payload = command.encode("ascii") + b"\n"
        if self.serial.write(payload) != len(payload):
            raise TransportError("usb_write_failed")
        rows = 3 if command == "STATUS" else 1
        received, total = [], 0
        deadline = self.clock() + 6
        while self.clock() < deadline:
            self.check()
            while b"\n" in self.pending:
                line, _, rest = self.pending.partition(b"\n")
                self.pending = bytearray(rest)
                if not line.startswith(b"{"):
                    continue
                received.append(self.json_parser(line))
                if len(received) == rows:
                    self.check()
                    self.next_send = self.clock() + .35
                    return received if rows == 3 else received[0]
            chunk = self.serial.read(1024)
            total += len(chunk)
            self.pending.extend(chunk)
            if total > 16384 or len(self.pending) > 8192:
                raise TransportError("usb_response_too_large")
        raise TransportError("usb_timeout")

    def run(self):
        status, radio, lifecycle = status_rows(self.request("STATUS"), reviewed_firmware=self.reviewed_firmware)
        boot = typed(self.request("BOOT_STATUS"), integers={"uptime_ms": U32, "reset_reason": (0, 255)})
        memory = typed(self.request("MEMORY_STATUS"), booleans=MEMORY_BOOLS,
                       integers={key: U32 for key in MEMORY_INTS})
        backup = typed(self.request("BACKUP_STATUS"), booleans=BACKUP_BOOLS, integers=BACKUP_INTS)
        # Preserve full wire precision across JSON/QWebChannel/JavaScript.
        for key in ("done", "total"):
            backup[key] = str(backup[key])
        dashboard = dashboard_row(self.request("DASHBOARD_STATUS"))
        self.check()
        return {"schema": "cloud-stick-diagnostics/v1", "snapshot_kind": "sequential_read_only",
                "status": status, "radio": radio, "lifecycle": lifecycle,
                "boot": boot, "memory": memory, "backup": backup, "dashboard": dashboard}
