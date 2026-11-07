# SPDX-License-Identifier: MIT
"""Native updater jobs, signed release cache and window-close coordination."""
import asyncio
import hashlib
import json
from pathlib import Path
import secrets
import threading
import time
import sys
from .firmware_catalog import (BUNDLE_LIMIT, CATALOG_LIMIT, CATALOG_URL, FirmwareError,
                               download, load_bundle, load_catalog, require, strict_json)
from .firmware_platform import data_root, launch_worker, operation_lock, worker_alive, process_identity
from .firmware_recovery import private_directory, private_read, save_private

PHASES = frozenset(('idle', 'checking', 'preparing', 'prepared', 'boot_review', 'available', 'starting', 'connecting', 'backup_a', 'backup_b',
                   'checking_board', 'writing', 'verifying', 'verified', 'restarting', 'installed',
                   'already_current', 'failed', 'recovery_required'))
TERMINAL = frozenset(('installed', 'already_current', 'failed', 'recovery_required', 'boot_review'))


def bundled_root():
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1])) / 'firmware'


class FirmwareManager:
    def __init__(self, usb):
        self.usb = usb
        self.root = data_root()
        self.lock = threading.RLock()
        self._checking = False
        self._state = {'phase': 'idle', 'done': 0, 'total': 1, 'code': '', 'online': False}
        self._ports = {}
        self._prepared = None
        self._approvals = {}
        self.catalog_raw = (bundled_root() / 'catalog.json').read_bytes()
        self.catalog = load_catalog(self.catalog_raw)
        self.bundle_path = bundled_root() / self.catalog.latest.asset
        self.job = None
        active = self.root / 'active.json'
        if active.exists():
            self.job = strict_json(private_read(active, 4096), 4096)
            require(set(self.job) == {'name', 'unit', 'pid', 'created'} and type(self.job['name']) is str
                    and __import__('re').fullmatch('[0-9a-f]{32}', self.job['name']), 'firmware_job_invalid')
            require(self.job['unit'] == 'cloud-stick-firmware-' + self.job['name'], 'firmware_job_invalid')
        cached = self.root / 'catalog.json'
        if cached.exists():
            raw = private_read(cached, CATALOG_LIMIT)
            catalog = load_catalog(raw, minimum_revision=self.catalog.revision)
            self.catalog, self.catalog_raw = catalog, raw
            self.bundle_path = self.root / catalog.latest.asset
        self._state['version'] = self.catalog.latest.version
        if self.job is not None:
            original = load_catalog(private_read(self._jobdir() / 'catalog.json', CATALOG_LIMIT))
            self._state['version'] = original.latest.version

    def _jobdir(self):
        return self.root / self.job['name']

    def state(self):
        with self.lock:
            if self.job is not None:
                path = self._jobdir() / 'progress.json'
                if path.exists():
                    row = strict_json(private_read(path, 4096), 4096)
                    require(type(row) is dict and set(row) == {'phase', 'done', 'total', 'code', 'writing'}
                            and row['phase'] in PHASES and type(row['done']) is int and type(row['total']) is int
                            and 0 <= row['done'] <= row['total'] <= 64 * 1024 * 1024
                            and type(row['writing']) is bool and type(row['code']) is str
                            and __import__('re').fullmatch('[a-z_]{0,64}', row['code']), 'firmware_job_invalid')
                    alive = worker_alive(self.job)
                    if row['phase'] not in TERMINAL and not alive:
                        # Launch may take a moment to register. An abandoned job
                        # never silently starts again or gains success status.
                        if row['writing'] or time.time() - path.stat().st_mtime > 20:
                            row.update(phase='recovery_required' if row['writing'] else 'failed', code='firmware_worker_stopped')
                            save_private(path, json.dumps(row).encode())
                    self._state.update({key: row[key] for key in ('phase', 'done', 'total', 'code')})
                    self._state['can_resume'] = row['writing'] and row['phase'] in ('failed', 'recovery_required') and not alive
            return dict(self._state)

    @property
    def running(self):
        with self.lock:
            return self._checking or (self.job is not None and worker_alive(self.job))

    @property
    def blocked(self):
        return self.running or self.state().get('can_resume', False)

    def ports(self):
        require(not self.running, 'firmware_busy')
        self._ports = self.usb.ports()
        return [{'port': port, 'label': port + ' · ESP32-S3 USB'} for port in self._ports]

    def check(self):
        with self.lock:
            require(not self.blocked, 'firmware_busy')
            self.job = None
            self._checking = True
            self._state.update(phase='checking', code='', done=0, total=1, can_resume=False)
        def run():
            online, code = False, ''
            try:
                raw = download(CATALOG_URL, CATALOG_LIMIT, catalog=True)
                catalog = load_catalog(raw, minimum_revision=self.catalog.revision)
                package = download(catalog.latest.url, catalog.latest.bundle_size)
                load_bundle(package, catalog.latest)
                with self.lock:
                    save_private(self.root / catalog.latest.asset, package)
                    # Catalog last: a crash cannot select a missing package.
                    save_private(self.root / 'catalog.json', raw)
                    self.catalog, self.catalog_raw, self.bundle_path = catalog, raw, self.root / catalog.latest.asset
                    online = True
            except FirmwareError as error:
                code = error.code
            except Exception:
                code = 'firmware_download_failed'
            finally:
                with self.lock:
                    try:
                        package = self.bundle_path.read_bytes() if self.bundle_path.stat().st_size <= BUNDLE_LIMIT else b''
                        load_bundle(package, self.catalog.latest)
                        phase = 'available'
                    except Exception:
                        phase, code = 'failed', 'firmware_package_unavailable'
                    self._state.update(phase=phase, code=code, online=online, version=self.catalog.latest.version, done=1, total=1)
                    self._checking = False
        threading.Thread(target=run, name='firmware-catalog', daemon=True).start()
        return self.state()

    def prepare(self, port):
        with self.lock:
            require(not self.blocked and self._state['phase'] == 'available' and port in self._ports, 'firmware_check_required')
            fingerprint = self._ports[port]
            self._checking = True
            self._state.update(phase='preparing', code='', done=0, total=1)
        def run():
            try:
                from .firmware_prepare import prepare
                status = self.usb.exchange(port, fingerprint, 'STATUS', threading.Event())
                require(status['firmware'] in {app.version for app in self.catalog.known_apps}, 'firmware_preimage_unknown')
                result = prepare(self.usb, port, fingerprint, threading.Event())
                with self.lock:
                    self._prepared = result
                    self._state.update(phase='prepared', done=1, total=1, code='')
            except Exception as error:
                code = getattr(error, 'code', 'firmware_pause_failed')
                with self.lock:
                    self._state.update(phase='failed', code=code, done=0, total=1)
            finally:
                with self.lock: self._checking = False
        threading.Thread(target=run, name='firmware-prepare', daemon=True).start()
        return self.state()

    def offer(self, request):
        # Called only by the native window before displaying its own consent.
        require(set(request) == {'id', 'op', 'port', 'mode'} and request['op'] == 'firmware_start', 'invalid_request')
        require(request['mode'] in ('existing', 'new', 'resume') and request['port'] in self._ports, 'invalid_request')
        require(not self.running, 'firmware_busy')
        state = self.state()
        if request['mode'] == 'resume':
            require(state.get('can_resume') is True, 'firmware_resume_unavailable')
            raw = private_read(self._jobdir() / 'catalog.json', CATALOG_LIMIT)
            target = load_catalog(raw).latest.version
        else:
            require(not state.get('can_resume') and state['phase'] == ('prepared' if request['mode'] == 'existing' else 'available'), 'firmware_check_required')
            target = self.catalog.latest.version
        token = secrets.token_hex(32)
        self._approvals.clear()
        self._approvals[token] = (dict(request), tuple(self._ports[request['port']]), hashlib.sha256(self.catalog_raw).hexdigest(), time.monotonic())
        return token, target

    def start(self, request, approval):
        with self.lock:
            grant = self._approvals.pop(approval, None)
            require(grant is not None and grant[0] == request and time.monotonic() - grant[3] < 300, 'firmware_native_approval_required')
            require(not self.running and self.usb.ports().get(request['port']) == grant[1], 'firmware_device_changed')
            mode = request['mode']
            if mode == 'resume':
                require(self.state().get('can_resume') is True, 'firmware_resume_unavailable')
                directory = self._jobdir()
                config = strict_json(private_read(directory / 'job.json', 8192), 8192)
                config.update(port=request['port'], fingerprint=list(grant[1]), resume=True)
            else:
                require(not self.state().get('can_resume') and self._state['phase'] == ('prepared' if mode == 'existing' else 'available')
                        and hashlib.sha256(self.catalog_raw).hexdigest() == grant[2], 'firmware_check_required')
                name = secrets.token_hex(16)
                directory = private_directory(self.root / name)
                package = self.bundle_path.read_bytes()
                load_bundle(package, self.catalog.latest)
                save_private(directory / 'catalog.json', self.catalog_raw)
                save_private(directory / 'package.cspfw', package)
                if mode == 'existing':
                    from .firmware_prepare import board_serial
                    require(self._prepared is not None and self._prepared['board_serial'] == board_serial(grant[1]), 'firmware_device_changed')
                config = {'port': request['port'], 'fingerprint': list(grant[1]), 'mode': mode, 'resume': False,
                          'prepared': self._prepared if mode == 'existing' else None}
                self.job = {'name': name, 'unit': 'cloud-stick-firmware-' + name, 'pid': None, 'created': None}
            save_private(directory / 'job.json', json.dumps(config).encode())
            save_private(directory / 'progress.json', json.dumps({'phase': 'starting', 'done': 0, 'total': 1, 'code': '', 'writing': mode == 'resume'}).encode())
            save_private(self.root / 'active.json', json.dumps(self.job).encode())
            try:
                self.job['pid'] = launch_worker(directory, self.job['unit'])
                self.job['created'] = process_identity(self.job['pid']) if self.job['pid'] is not None else None
                save_private(self.root / 'active.json', json.dumps(self.job).encode())
            except Exception:
                save_private(directory / 'progress.json', json.dumps({'phase': 'recovery_required' if mode == 'resume' else 'failed',
                    'done': 0, 'total': 1, 'code': 'firmware_worker_start_failed', 'writing': mode == 'resume'}).encode())
                raise
            return self.state()

    async def wait(self):
        while self.running:
            await asyncio.sleep(.5)
