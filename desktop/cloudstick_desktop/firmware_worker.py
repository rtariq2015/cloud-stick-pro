# SPDX-License-Identifier: GPL-3.0-or-later
"""Detached private installer entry; imported before Qt, Bleak or esptool."""
import json
import os
from pathlib import Path
import re
import signal
import sys
import threading
import time

from .firmware_catalog import BUNDLE_LIMIT, CATALOG_LIMIT, FirmwareError, load_catalog, require, strict_json
from .firmware_platform import data_root, ensure_rom_storage_absent, operation_lock, process_identity
from .firmware_recovery import RecoveryStore, private_directory, private_read, require_no_swap, save_private


def run(path):
    # Suppress dependency diagnostics before importing esptool. Vendor errors can
    # contain MACs and flash bytes. Only fixed status codes are written below.
    sink = os.open(os.devnull, os.O_RDWR)
    for fd in (1, 2):
        try: os.dup2(sink, fd)
        except OSError: pass
    os.close(sink)
    if sys.platform != 'win32':
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for name in ('SIGTERM', 'SIGINT', 'SIGHUP'):
        sig = getattr(signal, name, None)
        if sig is not None:
            signal.signal(sig, lambda *_: None)  # no termination mid-write
    directory = Path(path).absolute()
    root = data_root()
    require(directory.parent == root and re.fullmatch('[0-9a-f]{32}', directory.name), 'firmware_job_invalid')
    private_directory(directory)
    # The child records its own identity as well. A parent crash immediately
    # after spawn must not leave an unobservable Windows installer.
    save_private(root / 'active.json', json.dumps({'name': directory.name,
        'unit': 'cloud-stick-firmware-' + directory.name, 'pid': os.getpid(),
        'created': process_identity(os.getpid())}).encode())
    writing = False
    verified = False
    device = store = fresh = None

    def progress(phase, done=0, total=1, code=''):
        save_private(directory / 'progress.json', json.dumps({'phase': phase, 'done': done, 'total': total,
                                                            'code': code, 'writing': writing}).encode())

    def mark_writing():
        nonlocal writing
        writing = True
        # This marker can only ever move forward, including across worker death.
        save_private(directory / 'write-started', b'1')
        progress('writing')

    try:
        require_no_swap()
        if sys.platform == 'win32':
            import ctypes
            require(ctypes.windll.kernel32.SetThreadExecutionState(0x80000001), 'firmware_sleep_block_failed')
        config = strict_json(private_read(directory / 'job.json', 8192), 8192)
        require(set(config) == {'port', 'fingerprint', 'mode', 'resume', 'prepared'} and type(config['resume']) is bool
                and config['mode'] in ('new', 'existing') and type(config['fingerprint']) is list
                and len(config['fingerprint']) == 4, 'firmware_job_invalid')
        writing = (directory / 'write-started').exists()
        require(config['resume'] == writing, 'firmware_resume_required')
        catalog = load_catalog(private_read(directory / 'catalog.json', CATALOG_LIMIT))
        package = private_read(directory / 'package.cspfw', BUNDLE_LIMIT)
        from .firmware_catalog import load_bundle
        load_bundle(package, catalog.latest)
        # Pin vendor wire retries before its first import. No user/esptool
        # configuration can introduce a reset or retry after uncertain writes.
        save_private(directory / 'esptool.cfg', b'[esptool]\nwrite_block_attempts = 1\nconnect_attempts = 1\n')
        os.environ['ESPTOOL_CFGFILE'] = str(directory / 'esptool.cfg')
        from .firmware_engine import ROMDevice, install
        from . import firmware_usb as usb
        with operation_lock(root):
            progress('connecting')
            ensure_rom_storage_absent(config['port'])
            device = ROMDevice(config['port'], tuple(config['fingerprint']), usb.ports)
            if config['mode'] == 'existing':
                require(type(config['prepared']) is dict and set(config['prepared']) == {'board_serial', 'revision', 'owner_ready'}
                        and config['prepared']['board_serial'] == device.mac and config['prepared']['owner_ready'] is True,
                        'firmware_device_changed')
            metadata = {'board_mac': device.mac, 'catalog_sha256': catalog.signed_sha256,
                        'mode': config['mode'], 'target': catalog.latest.application.sha256, 'prepared': config['prepared']}
            if config['resume']:
                # Match original MAC, target and mode before reading private data.
                store = RecoveryStore(directory / 'recovery', metadata, create=False)
                original = store.chunks()
                try: next(original)  # authenticated metadata binds original MAC first
                finally: original.close()
                fresh = RecoveryStore(private_directory(directory / ('attempt-' + __import__('secrets').token_hex(8))), metadata)
                fresh.capture(device.read, progress)
            else:
                store = RecoveryStore(private_directory(directory / 'recovery'), metadata)
                store.capture(device.read, progress)
            progress('checking_board')
            result = install(device, store, catalog, package, config['mode'], progress, mark_writing, resume=config['resume'])
            verified = True
            save_private(directory / 'flash-verified', b'1')
            progress('verified', 1, 1)
            device.normal_boot()
            device.close()
            device = None
            progress('restarting')
            # Verify the running image and owner state. Network/backups are not
            # acceptance criteria here and are never reported as verified.
            deadline = time.monotonic() + 90
            ready = False
            while time.monotonic() < deadline:
                time.sleep(2)
                ports = usb.ports()
                match = []
                for p, fp in ports.items():
                    try:
                        if type(fp[2]) is str and re.sub('[:-]', '', fp[2]).lower() == metadata['board_mac']: match.append(p)
                    except FirmwareError: pass
                if len(match) != 1: continue
                try:
                    status, owner = usb.normal_status(match[0], ports[match[0]])
                    if (status['firmware'] != catalog.latest.version or status['store_fault']
                            or (config['mode'] == 'existing' and status['saved_revision'] != config['prepared']['revision'])):
                        continue
                    require(owner.get('owner_state') == (1 if config['mode'] == 'new' else 0), 'firmware_owner_check_failed')
                    ready = True
                    break
                except Exception:
                    continue
            require(ready, 'firmware_boot_check_failed')
            progress(result, 1, 1)
        return 0
    except BaseException as error:
        code = getattr(error, 'code', 'firmware_install_failed')
        if type(code) is not str or not re.fullmatch('[a-z_]{1,64}', code):
            code = 'firmware_install_failed'
        try: progress('boot_review' if verified else ('recovery_required' if writing else 'failed'), code=code)
        except Exception: pass
        return 1
    finally:
        if store is not None: store.close()
        if fresh is not None: fresh.close()
        if device is not None:
            try: device.close()
            except Exception: pass
        if sys.platform == 'win32':
            try: __import__('ctypes').windll.kernel32.SetThreadExecutionState(0x80000000)
            except Exception: pass


def self_test(report):
    """Packaged dependency/native API proof with public synthetic data only."""
    result = {'schema': 'cloud-stick-installer-self-test/v1', 'passed': False,
              'hardware_contact': False, 'platform': sys.platform, 'esptool': '5.4.0'}
    try:
        import tempfile
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from .firmware_recovery import dpapi, locked
        key = bytes(range(32)); nonce = bytes(range(12)); data = b'PUBLIC SYNTHETIC TEST ONLY'
        cipher = AESGCM(key)
        require(cipher.decrypt(nonce, cipher.encrypt(nonce, data, b'test'), b'test') == data)
        signer = Ed25519PrivateKey.from_private_bytes(key)
        signer.public_key().verify(signer.sign(data), data)
        with tempfile.TemporaryDirectory(prefix='cloud-stick-synthetic-') as temporary:
            root = private_directory(Path(temporary) / 'private')
            save_private(root / 'esptool.cfg', b'[esptool]\nwrite_block_attempts = 1\nconnect_attempts = 1\n')
            os.environ['ESPTOOL_CFGFILE'] = str(root / 'esptool.cfg')
            import esptool.loader
            from esptool.targets.esp32s3 import ESP32S3ROM
            require(esptool.loader.WRITE_BLOCK_ATTEMPTS == 1 and callable(ESP32S3ROM.read_flash_slow))
            if sys.platform == 'win32':
                require(dpapi(dpapi(key), decrypt=True) == key)
                with locked(bytearray(65536)): pass
                identity = process_identity(os.getpid())
                require(type(identity) is int)
                from .firmware_platform import worker_alive
                require(worker_alive({'pid': os.getpid(), 'created': identity}))
                require(not worker_alive({'pid': os.getpid(), 'created': identity + 1}))
                result['windows_private_apis'] = True
            else:
                try:
                    require_no_swap(); result['linux_no_swap_gate_available'] = True
                except FirmwareError:
                    result['linux_no_swap_gate_available'] = False
        result['passed'] = True
    except Exception:
        result['passed'] = False
    Path(report).write_text(json.dumps(result, indent=2) + '\n')
    return 0 if result['passed'] else 1


def main():
    if len(sys.argv) == 4 and sys.argv[1:3] == ['--self-test', '--report']:
        return self_test(sys.argv[3])
    if len(sys.argv) != 3 or sys.argv[1] != '--firmware-worker':
        return 2
    return run(sys.argv[2])
