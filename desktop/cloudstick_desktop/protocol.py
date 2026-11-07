"""Small, secret-free native bridge contract. No hardware imports."""

import re

SERVICE = "021a9004-0382-4aea-bff4-6b3f1c5adfb4"
CHARACTERISTICS = frozenset(f"021aff5{i}-0382-4aea-bff4-6b3f1c5adfb4" for i in range(1, 5))
MAX_BYTES = 512


class TransportError(Exception):
    """Only this fixed code, never an OS exception message, crosses the bridge."""

    def __init__(self, code):
        self.code = code
        super().__init__("The local device operation ended.")


def validate_request(request):
    fields = {
        "discover": (), "connect": ("device_id",), "read": ("uuid",),
        "write": ("uuid", "hex"), "disconnect": (), "usb_ports": (),
        "usb_status": ("port",), "usb_open_setup": ("port",),
        "usb_setup_access_status": ("port",), "usb_setup_access": ("port",),
        "usb_forget_setup_access": ("port",),
        "usb_owner_status": ("port",), "usb_owner_identity": ("port",),
        "usb_owner_install": ("port", "record_hex"),
        "usb_diagnostics": ("port",),
        "firmware_state": (), "firmware_ports": (), "firmware_check": (),
        "firmware_prepare": ("port",), "firmware_start": ("port", "mode"),
    }
    if type(request) is not dict or type(request.get("op")) is not str:
        raise TransportError("invalid_request")
    op = request["op"]
    if op not in fields or set(request) - {"op", "id", *fields[op]}:
        raise TransportError("invalid_request")
    if not set(fields[op]).issubset(request):
        raise TransportError("invalid_request")
    if "id" in request and (type(request["id"]) is not int or not 1 <= request["id"] <= 2**53 - 1):
        raise TransportError("invalid_request")
    for key in ("device_id", "port"):
        if key in request and (type(request[key]) is not str or not 1 <= len(request[key]) <= 256
                               or any(ord(c) < 32 or ord(c) == 127 for c in request[key])):
            raise TransportError("invalid_request")
    if 'mode' in request and request['mode'] not in ('new', 'existing', 'resume'):
        raise TransportError('invalid_request')
    if "uuid" in request and (type(request["uuid"]) is not str or request["uuid"] not in CHARACTERISTICS):
        raise TransportError("invalid_characteristic")
    if "hex" in request and (type(request["hex"]) is not str
                              or not re.fullmatch(r"(?:[0-9a-f]{2}){1,512}", request["hex"])):
        raise TransportError("invalid_payload")
    if "record_hex" in request:
        from .enrollment import validate_record
        validate_record(request["record_hex"])
    return op


def diagnostic_status(row):
    """STATUS's first row only. Extra diagnostics or private fields are dropped."""
    if type(row) is not dict:
        raise TransportError("invalid_usb_response")
    result = {}
    for key in ("setup_only", "owner_ready", "store_fault", "ble", "internet_verified", "backups_paused"):
        if type(row.get(key)) is not bool:
            raise TransportError("invalid_usb_response")
        result[key] = row[key]
    firmware = row.get("firmware")
    # Bounded ASCII prerelease identifiers may contain internal hyphens, as in
    # the real setup candidate. Dots separate nonempty identifiers.
    version = (re.fullmatch(r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}(?:-([a-z0-9-]+(?:\.[a-z0-9-]+)*))?", firmware)
               if type(firmware) is str and len(firmware) <= 36 else None)
    if version is None or (version.group(1) is not None and len(version.group(1)) > 24):
        raise TransportError("invalid_usb_response")
    result["firmware"] = firmware
    if row.get("wifi") not in ("idle", "scanning", "connecting", "local_connected", "failed", "fault", "cancelling"):
        raise TransportError("invalid_usb_response")
    result["wifi"] = row["wifi"]
    revision = row.get("saved_revision")
    if type(revision) is not str or not re.fullmatch(r"[0-9]{1,20}", revision) or int(revision) >= 2**64:
        raise TransportError("invalid_usb_response")
    result["saved_revision"] = revision
    for key in ("internal_free", "largest_block"):
        if type(row.get(key)) is not int or not 0 <= row[key] < 2**32:
            raise TransportError("invalid_usb_response")
        result[key] = row[key]
    return result
