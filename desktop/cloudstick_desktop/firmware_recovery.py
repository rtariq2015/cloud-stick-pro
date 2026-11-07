# SPDX-License-Identifier: MIT
"""Private, authenticated recovery streams. Never write plaintext flash to disk."""
from contextlib import contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import stat
import struct
import sys
import threading

from .firmware_catalog import FLASH_BYTES, FirmwareError, require, strict_json

CHUNK = 64 * 1024
MAGIC = b'CSPREC01'
_PAGE_LOCK = threading.RLock()
_LOCKED_PAGES = {}


def private_directory(path):
    path = Path(path).absolute()
    require(not path.is_symlink(), 'recovery_directory_unsafe')
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    for parent in (path, *path.parents):
        require(not parent.is_symlink() and not (getattr(parent.lstat(), 'st_file_attributes', 0) & 0x400), 'recovery_directory_unsafe')
    if sys.platform == 'win32':
        # A protected DACL grants only this user and SYSTEM access. Do not rely on
        # chmod (which does not set NTFS access control).
        import subprocess
        result = subprocess.run(['whoami', '/user', '/fo', 'csv', '/nh'], capture_output=True, timeout=5, check=True)
        import re
        sid = re.search(rb'S-1-5-[0-9-]+', result.stdout)
        require(sid is not None, 'recovery_directory_unsafe')
        adv = ctypes.WinDLL('advapi32', use_last_error=True)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        descriptor = ctypes.c_void_p()
        adv.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]
        adv.SetFileSecurityW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_void_p]
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        sddl = 'D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;' + sid.group().decode('ascii') + ')'
        require(adv.ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, ctypes.byref(descriptor), None), 'recovery_directory_unsafe')
        try:
            require(adv.SetFileSecurityW(str(path), 0x80000004, descriptor), 'recovery_directory_unsafe')
        finally:
            kernel.LocalFree(descriptor)
    else:
        row = path.stat()
        require(row.st_uid == os.getuid() and stat.S_IMODE(row.st_mode) == 0o700, 'recovery_directory_unsafe')
    return path


@contextmanager
def private_input(path, limit):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_BINARY', 0)
    require(not Path(path).is_symlink() and not (getattr(Path(path).lstat(), 'st_file_attributes', 0) & 0x400), 'recovery_file_unsafe')
    with os.fdopen(os.open(path, flags), 'rb') as stream:
        row = os.fstat(stream.fileno())
        require(stat.S_ISREG(row.st_mode) and row.st_size <= limit, 'recovery_file_unsafe')
        if os.name == 'posix':
            require(row.st_uid == os.getuid() and stat.S_IMODE(row.st_mode) == 0o600 and row.st_nlink == 1, 'recovery_file_unsafe')
        yield stream


def private_read(path, limit):
    with private_input(path, limit) as stream:
        return stream.read(limit + 1)


@contextmanager
def private_output(path):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_BINARY', 0), 0o600)
    with os.fdopen(fd, 'wb') as stream:
        yield stream
        stream.flush()
        os.fsync(stream.fileno())


def save_private(path, data):
    path = Path(path)
    temp = path.with_name('.' + path.name + '-' + secrets.token_hex(8))
    with private_output(temp) as out:
        out.write(data)
    os.replace(temp, path)
    if os.name == 'posix':
        fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try: os.fsync(fd)
        finally: os.close(fd)


def require_no_swap():
    if sys.platform == 'linux':
        # Read the effective limits of this actual process, not a launcher's claim.
        rows = Path('/proc/self/cgroup').read_text().splitlines()
        paths = [row[3:] for row in rows if row.startswith('0::/')]
        require(len(paths) == 1 and '..' not in Path(paths[0]).parts, 'private_memory_unavailable')
        root = Path('/sys/fs/cgroup') / paths[0].lstrip('/')
        try:
            safe = (root.joinpath('memory.swap.max').read_text().strip() == '0'
                    and root.joinpath('memory.swap.current').read_text().strip() == '0')
        except OSError:
            safe = False
        require(safe, 'private_memory_unavailable')
    elif sys.platform != 'win32':
        raise FirmwareError('unsupported_platform')


@contextmanager
def locked(data):
    """Windows locks the caller's buffer; Python/crypto temporaries have limits.

    Linux's required cgroup gate covers the complete worker, including temporary
    library copies. No claim of whole-process no-swap is made on Windows.
    """
    if sys.platform != 'win32':
        yield data
        return
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.VirtualLock.argtypes = kernel.VirtualUnlock.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetProcessWorkingSetSize.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t]
    kernel.SetProcessWorkingSetSize(kernel.GetCurrentProcess(), 8 * 1024 * 1024, 64 * 1024 * 1024)
    if isinstance(data, bytes):
        api = ctypes.pythonapi.PyBytes_AsString
        api.argtypes, api.restype = [ctypes.py_object], ctypes.c_void_p
        address = api(data)
    else:
        address = ctypes.addressof((ctypes.c_ubyte * len(data)).from_buffer(data))
    # VirtualLock has no OS lock count. Refcount pages here so an inner key or
    # overlapping buffer cannot unlock an outer private region prematurely.
    pages = list(range(address & ~4095, (address + len(data) + 4095) & ~4095, 4096))
    acquired = []
    def release():
        for page in reversed(acquired):
            _LOCKED_PAGES[page] -= 1
            if _LOCKED_PAGES[page] == 0:
                kernel.VirtualUnlock(page, 4096)
                del _LOCKED_PAGES[page]
    with _PAGE_LOCK:
        try:
            for page in pages:
                if page not in _LOCKED_PAGES:
                    require(kernel.VirtualLock(page, 4096), 'private_memory_unavailable')
                    _LOCKED_PAGES[page] = 0
                _LOCKED_PAGES[page] += 1
                acquired.append(page)
        except BaseException:
            release()
            raise
    try: yield data
    finally:
        with _PAGE_LOCK: release()


def dpapi(data, *, decrypt=False):
    class Blob(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    source, result = Blob(len(data), buffer), Blob()
    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    method = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    method.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Blob)]
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    require(method(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(result)), 'recovery_key_unavailable')
    try: return ctypes.string_at(result.data, result.size)
    finally:
        ctypes.memset(buffer, 0, len(buffer))
        ctypes.memset(result.data, 0, result.size)
        kernel.LocalFree(result.data)


class RecoveryStore:
    def __init__(self, directory, metadata, *, create=True):
        require_no_swap()
        self.directory = private_directory(directory)
        self.metadata = metadata
        key_path = self.directory / 'recovery.key'
        if create:
            require(not key_path.exists(), 'recovery_checkpoint_exists')
            key = secrets.token_bytes(32)
            with private_output(key_path) as out:
                out.write(dpapi(key) if sys.platform == 'win32' else key)
        else:
            raw = private_read(key_path, 4096)
            key = dpapi(raw, decrypt=True) if sys.platform == 'win32' else raw
        require(len(key) == 32, 'recovery_key_unavailable')
        with locked(key):
            self.key = bytearray(key)
            self._key_lock = locked(self.key)
            try: self._key_lock.__enter__()
            except BaseException:
                self.key[:] = b'\0' * len(self.key)
                raise

    def close(self):
        self.key[:] = b'\0' * len(self.key)
        if self._key_lock is not None:
            self._key_lock.__exit__(None, None, None)
            self._key_lock = None

    def capture(self, read, progress, *, prefix=''):
        require(shutil.disk_usage(self.directory).free >= 40 * 1024 * 1024, 'recovery_disk_full')
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        with locked(self.key):
            cipher = AESGCM(self.key)
            prefixes = set()
            for copy in ('a', 'b'):
                nonce = secrets.token_bytes(8)
                require(nonce not in prefixes, 'recovery_nonce_collision')
                prefixes.add(nonce)
                header = MAGIC + nonce
                meta = json.dumps(self.metadata, sort_keys=True, separators=(',', ':')).encode()
                require(len(meta) <= 4096, 'recovery_metadata_invalid')
                with private_output(self.directory / (prefix + copy + '.csprec')) as output:
                    encrypted = cipher.encrypt(nonce + struct.pack('>I', 0), meta, header)
                    output.write(header + struct.pack('>I', len(encrypted)) + encrypted)
                    for index, offset in enumerate(range(0, FLASH_BYTES, CHUNK), 1):
                        data = read(offset, CHUNK)
                        require(type(data) is bytes and len(data) == CHUNK, 'firmware_read_incomplete')
                        with locked(data):
                            output.write(cipher.encrypt(nonce + struct.pack('>I', index), data, header + struct.pack('>I', index)))
                        progress('backup_' + copy, offset + CHUNK, FLASH_BYTES)
                        del data
            # Independently authenticate/reopen both streams and compare every
            # byte before returning any write authority.
            digest = hashlib.sha256()
            for a, b in zip(self.chunks(prefix + 'a'), self.chunks(prefix + 'b'), strict=True):
                with locked(a), locked(b):
                    require(a == b, 'recovery_reads_differ')
                    digest.update(a)
            return digest.hexdigest()

    def chunks(self, name='a'):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        path = self.directory / (name + '.csprec')
        require(not path.is_symlink(), 'recovery_file_unsafe')
        try:
            with locked(self.key), private_input(path, FLASH_BYTES + 16 * 1024) as source:
                cipher = AESGCM(self.key)
                header = source.read(16)
                require(header[:8] == MAGIC and len(header) == 16, 'recovery_checkpoint_invalid')
                size_raw = source.read(4)
                require(len(size_raw) == 4, 'recovery_checkpoint_invalid')
                size = struct.unpack('>I', size_raw)[0]
                require(16 <= size <= 4112, 'recovery_checkpoint_invalid')
                meta = cipher.decrypt(header[8:] + struct.pack('>I', 0), source.read(size), header)
                require(strict_json(meta, 4096) == self.metadata, 'recovery_checkpoint_invalid')
                for index in range(1, FLASH_BYTES // CHUNK + 1):
                    data = source.read(CHUNK + 16)
                    require(len(data) == CHUNK + 16, 'recovery_checkpoint_invalid')
                    plaintext = cipher.decrypt(header[8:] + struct.pack('>I', index), data, header + struct.pack('>I', index))
                    with locked(plaintext): yield plaintext
                require(source.read(1) == b'', 'recovery_checkpoint_invalid')
        except FirmwareError:
            raise
        except Exception:
            raise FirmwareError('recovery_checkpoint_invalid') from None

    def range(self, offset, size):
        require(type(offset) is int and type(size) is int and 0 <= offset < FLASH_BYTES and 0 < size <= FLASH_BYTES - offset)
        out = bytearray()
        for index, data in enumerate(self.chunks()):
            start = index * CHUNK
            if start < offset + size and start + CHUNK > offset:
                out.extend(data[max(0, offset - start):min(CHUNK, offset + size - start)])
            if start + CHUNK >= offset + size:
                break
        require(len(out) == size, 'recovery_checkpoint_invalid')
        return bytes(out)
