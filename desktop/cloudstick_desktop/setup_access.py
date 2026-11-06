"""Protected, USB-bound local bootstrap. Never discovers/connects/opens a device.

Only the fixed per-user config chooses the existing credential file. All opens
are read-only descriptor-relative O_NOFOLLOW opens; renderer paths are absent.
"""
from pathlib import Path
import json
import os
import re
import stat
import sys

from .protocol import TransportError


class SetupAccess:
    def __init__(self, *, trusted_root=None):
        # The optional trust anchor exists for isolated synthetic tests only;
        # the production transport never accepts a renderer/environment path.
        self._test_root = Path(trusted_root) if trusted_root is not None else None
        if self._test_root is None and sys.platform == "linux":
            import pwd
            home = Path(pwd.getpwuid(os.getuid()).pw_dir)
        elif self._test_root is None:
            home = Path("/")  # Unsupported platforms fail closed before any open.
        else:
            home = self._test_root
        self._config = home / ".local/share/cloud-stick-desktop/setup-access-v1.json"

    @staticmethod
    def _stamp(value):
        return (value.st_dev, value.st_ino, value.st_mode, value.st_uid, value.st_gid,
                value.st_size, value.st_mtime_ns, value.st_ctime_ns, value.st_nlink)

    @staticmethod
    def _directory(value):
        if not stat.S_ISDIR(value.st_mode) or value.st_uid not in (0, os.getuid()):
            raise TransportError("setup_access_unavailable")
        if value.st_mode & 0o022:
            raise TransportError("setup_access_unavailable")

    def _read(self, path, bound, private_parent=False, *, remove=False, expected=None):
        if remove and (path != self._config or not private_parent or expected is None):
            raise TransportError("setup_access_unavailable")
        if (sys.platform != "linux" or not hasattr(os, "O_NOFOLLOW") or not hasattr(os, "O_DIRECTORY")
                or not path.is_absolute() or any(part in (".", "..") for part in path.parts)
                or len(str(path)) > 4096 or any(ord(c) < 32 or ord(c) == 127 for c in str(path))):
            raise TransportError("setup_access_unavailable")
        descriptors = []
        links = []
        file_descriptor = None
        try:
            flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_DIRECTORY
            anchor = self._test_root if self._test_root is not None else Path("/")
            if self._test_root is not None:
                if not anchor.is_absolute() or not path.is_relative_to(anchor):
                    raise TransportError("setup_access_unavailable")
                relative = path.relative_to(anchor).parts
            else:
                relative = path.parts[1:]
            parent = os.open(anchor, flags)
            descriptors.append(parent)
            self._directory(os.fstat(parent))
            if self._test_root is not None and (os.fstat(parent).st_uid != os.getuid() or stat.S_IMODE(os.fstat(parent).st_mode) != 0o700):
                raise TransportError("setup_access_unavailable")
            for component in relative[:-1]:
                child = os.open(component, flags, dir_fd=parent)
                descriptors.append(child)
                value = os.fstat(child)
                self._directory(value)
                links.append((parent, component, child, (value.st_dev, value.st_ino)))
                parent = child
            directory = os.fstat(parent)
            if private_parent and (directory.st_uid != os.getuid() or stat.S_IMODE(directory.st_mode) != 0o700):
                raise TransportError("setup_access_unavailable")
            file_descriptor = os.open(path.name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
            before = os.fstat(file_descriptor)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid()
                    or stat.S_IMODE(before.st_mode) != 0o600 or before.st_nlink != 1 or not 1 <= before.st_size <= bound):
                raise TransportError("setup_access_unavailable")
            payload = bytearray()
            while block := os.read(file_descriptor, min(4096, bound + 1 - len(payload))):
                payload.extend(block)
                if len(payload) > bound:
                    raise TransportError("setup_access_unavailable")
            after = os.fstat(file_descriptor)
            current = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
            if self._stamp(before) != self._stamp(after) or self._stamp(after) != self._stamp(current) or len(payload) != before.st_size:
                raise TransportError("setup_access_unavailable")
            for ancestor, component, child, identity in links:
                current = os.stat(component, dir_fd=ancestor, follow_symlinks=False)
                self._directory(current)
                if (current.st_dev, current.st_ino) != identity:
                    raise TransportError("setup_access_unavailable")
                self._directory(os.fstat(child))
            for descriptor in descriptors:
                self._directory(os.fstat(descriptor))
            # Recheck the private directory's permissions after the read too.
            directory = os.fstat(parent)
            if private_parent and (directory.st_uid != os.getuid() or stat.S_IMODE(directory.st_mode) != 0o700):
                raise TransportError("setup_access_unavailable")
            if expected is not None and bytes(payload) != expected:
                raise TransportError("setup_access_unavailable")
            if remove:
                os.unlink(path.name, dir_fd=parent)
                os.fsync(parent)
            return bytes(payload)
        finally:
            if file_descriptor is not None:
                os.close(file_descriptor)
            for descriptor in reversed(descriptors):
                os.close(descriptor)

    @staticmethod
    def _json(payload):
        def unique(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise TransportError("setup_access_unavailable")
                result[key] = value
            return result
        return json.loads(payload, object_pairs_hook=unique)

    def load(self, fingerprint):
        return self._load(fingerprint)

    def _load(self, fingerprint, *, forget=False):
        try:
            original = self._read(self._config, 4096, private_parent=True)
            config = self._json(original)
            if type(config) is not dict or set(config) != {"version", "usb", "device_id", "credential_file"}:
                raise TransportError("setup_access_unavailable")
            usb = config["usb"]
            if (type(config["version"]) is not int or config["version"] != 1 or type(usb) is not dict
                    or set(usb) != {"vid", "pid", "serial_number"}
                    or type(usb["vid"]) is not int or usb["vid"] != 12346
                    or type(usb["pid"]) is not int or usb["pid"] != 4097
                    or type(usb["serial_number"]) is not str or not 1 <= len(usb["serial_number"]) <= 256
                    or usb["serial_number"].strip() != usb["serial_number"]
                    or any(ord(c) < 32 or ord(c) > 126 for c in usb["serial_number"])):
                raise TransportError("setup_access_unavailable")
            if (type(fingerprint) is not tuple or len(fingerprint) != 4
                    or fingerprint[:3] != (usb["vid"], usb["pid"], usb["serial_number"])):
                raise TransportError("setup_access_device_mismatch")
            device = config["device_id"]
            if type(device) is not str or re.fullmatch(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}", device) is None:
                raise TransportError("setup_access_unavailable")
            name = config["credential_file"]
            if type(name) is not str or not name.startswith("/") or len(name) > 4096:
                raise TransportError("setup_access_unavailable")
            payload = self._read(Path(name), 256)
            if payload.endswith(b"\r\n"):
                payload = payload[:-2]
            elif payload.endswith((b"\n", b"\r")):
                payload = payload[:-1]
            # Match the existing legacy text/card importer exactly; do not
            # broaden it to whitespace or arbitrary printable punctuation.
            if re.fullmatch(rb"[A-Za-z0-9_-]{32}", payload) is None:
                raise TransportError("setup_access_invalid_key")
            if forget:
                self._read(self._config, 4096, private_parent=True, remove=True, expected=original)
                return {"forgotten": True}
            return {"key": payload.decode("ascii"), "device_id": device}
        except TransportError:
            raise
        except (OSError, ValueError, TypeError, RecursionError, UnicodeError):
            raise TransportError("setup_access_unavailable") from None

    def available(self, fingerprint):
        try:
            self.load(fingerprint)
            return True
        except TransportError:
            return False

    def forget(self, fingerprint):
        return self._load(fingerprint, forget=True)
