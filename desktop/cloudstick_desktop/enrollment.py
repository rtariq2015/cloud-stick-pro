"""Bounded first-owner USB protocol. No discovery, files, radio or reset APIs.

The firmware remains the authority for the physical hold, absent SD and idle
radio gates. A verifier is private material: never put records into exceptions,
diagnostics or returned values. A successful USB acknowledgment is followed by
an independent owner-state read; the caller still must prove the chosen login.
"""
import re
import struct
import time
import zlib

from .protocol import TransportError, diagnostic_status


# Exact reviewed profiles only. The incremental profile inherits setup-dev's
# owner/BLE/wire flags; its storage/LCD changes do not change this protocol.
REVIEWED_FIRMWARE = frozenset({"0.4.1-setup-dev", "0.4.2-incremental-dev", "0.4.3-ui-dev", "0.4.4-wifi-dev"})
OWNER_READY, OWNER_MISSING, OWNER_FAULT = 0, 1, 2
RECORD_BYTES = 432
# RFC5054 3072-bit group, identical to firmware owner_group.h / pinned SDK.
GROUP_HEX = (
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74"
    "020bbea63b139b22514a08798e3404ddef9519b3cd3a431b302b0a6df25f1437"
    "4fe1356d6d51c245e485b576625e7ec6f44c42e9a637ed6b0bff5cb6f406b7ed"
    "ee386bfb5a899fa5ae9f24117c4b1fe649286651ece45b3dc2007cb8a163bf05"
    "98da48361c55d39a69163fa8fd24cf5f83655d23dca3ad961c62f356208552bb"
    "9ed529077096966d670c354e4abc9804f1746c08ca18217c32905e462e36ce3b"
    "e39e772c180e86039b2783a2ec07a28fb5c55df06f4c52c9de2bcbf695581718"
    "3995497cea956ae515d2261898fa051015728e5a8aaac42dad33170d04507a33"
    "a85521abdf1cba64ecfb850458dbef0a8aea71575d060c7db3970f85a6e1e4c7"
    "abf5ae8cdb0933d71e8c94e04a25619dcee3d2261ad2ee6bf12ffa06d98a0864"
    "d87602733ec86a64521f2b18177b200cbbe117577a615d6c770988c0bad946e2"
    "08e24fa074e5ab3143db5bfce0fd108e4b82d120a93ad2caffffffffffffffff"
)
GROUP = bytes.fromhex(GROUP_HEX)


def owner_status(row):
    fields = {"owner_state", "boot_down", "held_ms", "blocked", "can_enroll", "last_blocked", "install_step"}
    if type(row) is not dict or set(row) != fields:
        raise TransportError("invalid_owner_status")
    if (type(row["owner_state"]) is not int or row["owner_state"] not in (0, 1, 2)
            or type(row["boot_down"]) is not bool or type(row["can_enroll"]) is not bool
            or type(row["held_ms"]) is not int or not 0 <= row["held_ms"] < 2**32
            or type(row["install_step"]) is not int or not 0 <= row["install_step"] <= 6):
        raise TransportError("invalid_owner_status")
    for key in ("blocked", "last_blocked"):
        if type(row[key]) is not int or not 0 <= row[key] <= 1023:
            raise TransportError("invalid_owner_status")
    if row["can_enroll"] != (row["blocked"] == 0):
        raise TransportError("invalid_owner_status")
    if row["can_enroll"] and (row["owner_state"] != OWNER_MISSING or not row["boot_down"] or row["held_ms"] < 10000):
        raise TransportError("invalid_owner_status")
    if row["owner_state"] != OWNER_MISSING and not row["blocked"] & 1:
        raise TransportError("invalid_owner_status")
    return dict(row)


def owner_identity(row):
    if (type(row) is not dict or set(row) != {"owner_device"}
            or type(row["owner_device"]) is not str
            or re.fullmatch(r"[0-9a-f]{12}", row["owner_device"]) is None
            or row["owner_device"] in ("000000000000", "ffffffffffff")):
        raise TransportError("invalid_owner_identity")
    return {"device": row["owner_device"]}


def validate_record(record_hex, device=None):
    if type(record_hex) is not str or re.fullmatch(r"[0-9a-f]{864}", record_hex) is None:
        raise TransportError("invalid_owner_record")
    record = bytearray.fromhex(record_hex)
    try:
        mac = record[6:12].hex()
        if (record[:6] != b"COW2\x02\x00" or not record[12] or not record[28]
                or bytes(record[28:412]) >= GROUP or any(record[412:428])
                or zlib.crc32(record[:428]) != struct.unpack_from("<I", record, 428)[0]
                or mac in ("000000000000", "ffffffffffff")):
            raise TransportError("invalid_owner_record")
        if device is not None and mac != device:
            raise TransportError("owner_record_device_mismatch")
        return mac
    finally:
        record[:] = b"\x00" * len(record)


class OwnerChannel:
    """One already-open, exclusive, non-reset serial session."""
    def __init__(self, serial_port, stop, same_device, json_parser, *, clock=None, wait=None):
        self.serial = serial_port
        self.stop = stop
        self.same_device = same_device
        self.json_parser = json_parser
        self.pending = bytearray()
        self.attempted = False
        self.clock = clock or time.monotonic
        self.wait = wait or stop.wait
        self.next_send = None

    def check(self):
        if self.stop.is_set():
            raise TransportError("cancelled")
        if not self.same_device():
            raise TransportError("usb_device_changed")

    def request(self, command, *, rows=1, installing=False):
        # Internal fixed commands only; the sole payload is a validated record.
        if command not in ("STATUS", "OWNER_ID", "OWNER_STATUS"):
            if not installing or not command.startswith("OWNER_INSTALL "):
                raise TransportError("invalid_request")
            validate_record(command[14:])
        self.check()
        # The actual firmware silently discards commands within250ms of its
        # previous serial reply. Pace from COMPLETED response consumption, not
        # from write/start; all three STATUS rows must have arrived first.
        if self.next_send is not None:
            while (remaining := self.next_send - self.clock()) > 0:
                if self.wait(min(remaining, 0.05)):
                    raise TransportError("cancelled")
                self.check()
        self.check()
        payload = bytearray(command, "ascii")
        payload.append(10)
        try:
            if installing:
                self.attempted = True  # Even a short/failed write is uncertain.
            if self.serial.write(payload) != len(payload):
                raise TransportError("usb_write_failed")
        finally:
            payload[:] = b"\x00" * len(payload)
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
                    self.next_send = self.clock() + 0.35
                    return received[0] if rows == 1 else received
            chunk = self.serial.read(1024)
            total += len(chunk)
            self.pending.extend(chunk)
            if total > 16384 or len(self.pending) > 8192:
                raise TransportError("usb_response_too_large")
        raise TransportError("usb_timeout")

    def firmware(self):
        # The reviewed STATUS command has exactly three JSON rows. Consume all
        # of them so delayed diagnostics cannot be mistaken for an owner reply.
        rows = self.request("STATUS", rows=3)
        status = diagnostic_status(rows[0])
        if status["firmware"] not in REVIEWED_FIRMWARE:
            raise TransportError("owner_firmware_unsupported")
        if (set(rows[1]) != {"wifi_step", "wifi_sdk_error", "wifi_error"}
                or "ble_before_free" not in rows[2] or "host_exit_stack_min" not in rows[2]):
            raise TransportError("invalid_usb_response")
        return status

    def run(self, op, record_hex=None):
        status = self.firmware()
        if op == "usb_owner_status":
            return owner_status(self.request("OWNER_STATUS"))
        identity = owner_identity(self.request("OWNER_ID"))
        if op == "usb_owner_identity":
            return identity
        if op != "usb_owner_install":
            raise TransportError("invalid_request")
        validate_record(record_hex, identity["device"])
        gate = owner_status(self.request("OWNER_STATUS"))
        if gate["owner_state"] != OWNER_MISSING:
            raise TransportError("owner_not_missing")
        if (status["store_fault"] or status["ble"] or not status["backups_paused"]
                or not gate["can_enroll"] or not gate["boot_down"]
                or gate["held_ms"] < 10000 or gate["blocked"] != 0):
            raise TransportError("owner_enrollment_blocked")
        reply = self.request("OWNER_INSTALL " + record_hex, installing=True)
        if type(reply) is not dict or set(reply) != {"owner_installed"} or reply["owner_installed"] is not True:
            raise TransportError("owner_install_unknown")
        verified = owner_status(self.request("OWNER_STATUS"))
        if verified["owner_state"] != OWNER_READY or verified["install_step"] != 6:
            raise TransportError("owner_install_unknown")
        return {"installed": True}
