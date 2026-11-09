# SPDX-License-Identifier: MIT
"""Signed companion-app releases; no device, password or renderer-selected URL."""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import ssl
import sys
import threading
import urllib.request

from . import __version__
from .firmware_catalog import SafeRedirect, strict_json
from .firmware_platform import data_root
from .firmware_recovery import private_directory, private_read, save_private

FEED = 'https://raw.githubusercontent.com/rtariq2015/cloud-stick-pro/main/desktop/updates/catalog.json'
KEY = bytes.fromhex('3059301306072a8648ce3d020106082a8648ce3d030107034200044711d64b9f6da526b474fa3dbd0f9a5dd1eb1cd0380760daf174dbe03d5478726ec16e79ab4bb31947584f7b4f4f73998306e126c36791030bb1b6fc378c28a5')
DOMAIN = b'CloudStickAppCatalog/v1\0'
LIMIT = 65536


def require(value):
    if not value:
        raise ValueError('app_update_invalid')


def catalog(raw, minimum=1, key=KEY):
    from cryptography.hazmat.primitives.serialization import load_der_public_key
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives import hashes
    envelope = strict_json(raw, LIMIT)
    require(set(envelope) == {'schema', 'payload', 'signature'} and envelope['schema'] == 'cloud-stick-app-signature/v1')
    payload = base64.b64decode(envelope['payload'], validate=True)
    signature = base64.b64decode(envelope['signature'], validate=True)
    require(len(payload) <= 32768 and 64 <= len(signature) <= 72)
    load_der_public_key(key).verify(signature, DOMAIN + payload, ec.ECDSA(hashes.SHA256()))
    data = strict_json(payload, LIMIT)
    require(set(data) == {'schema', 'revision', 'releases'} and data['schema'] == 'cloud-stick-app-catalog/v1')
    require(type(data['revision']) is int and minimum <= data['revision'] <= 2147483647)
    require(type(data['releases']) is list and 1 <= len(data['releases']) <= 4)
    seen = set()
    for row in data['releases']:
        require(type(row) is dict and set(row) == {'platform', 'version', 'code', 'tag', 'asset', 'size', 'sha256', 'source'})
        require(row['platform'] in ('linux-user', 'linux-deb', 'windows', 'android') and row['platform'] not in seen)
        seen.add(row['platform'])
        require(type(row['version']) is str and re.fullmatch(r'\d{1,3}\.\d{1,3}\.\d{1,3}(?:-[a-z0-9.-]{1,24})?', row['version']))
        require(type(row['code']) is int and 1 <= row['code'] <= 2147483647)
        require(type(row['size']) is int and 1024 <= row['size'] <= 1024 * 1024 * 1024)
        for field, pattern in [('tag', r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}'), ('asset', r'(?:cloud-stick[-_]|Cloud-Stick-)[A-Za-z0-9._-]{1,120}'), ('sha256', r'[0-9a-f]{64}'), ('source', r'[0-9a-f]{40}')]:
            require(type(row[field]) is str and re.fullmatch(pattern, row[field]))
        require(row['asset'].endswith({'linux-user': '.tar.gz', 'linux-deb': '.deb', 'windows': '.exe', 'android': '.apk'}[row['platform']]))
    return data


def version(value):
    # Development suffixes never make a newer numeric release look older.
    return tuple(int(n) for n in value.split('-')[0].split('.'))


def platform():
    if sys.platform == 'win32':
        return 'windows'
    prefix = Path(sys.executable).resolve().parent
    return 'linux-user' if (prefix / '.cloud-stick-user-install').is_file() else 'linux-deb'


def opener():
    return urllib.request.build_opener(SafeRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))


class AppUpdates:
    def __init__(self, root=None, target=None):
        self.root = root or data_root() / 'app-updates'
        self.target = target or platform()
        self.lock = threading.RLock()
        self.busy = False
        self.release = None
        self.path = None
        self.status = {'phase': 'idle', 'current': __version__, 'version': '', 'done': 0, 'total': 1}

    def state(self):
        with self.lock:
            return dict(self.status)

    def start(self, download=False, imported=None):
        with self.lock:
            require(not self.busy)
            if download:
                require(self.release is not None and self.status['phase'] == 'available')
            self.busy = True
            self.status.update(phase='downloading' if download else 'checking', done=0, total=1)

        def run():
            try:
                private_directory(self.root)
                if download:
                    self._download()
                else:
                    minimum = 1
                    cache = self.root / 'catalog.json'
                    if cache.exists():
                        minimum = catalog(private_read(cache, LIMIT))['revision']
                    if imported is None:
                        with opener().open(urllib.request.Request(FEED, headers={'User-Agent': 'CloudStickSetup-Updates'}), timeout=15) as reply:
                            require(reply.status == 200)
                            raw = reply.read(LIMIT + 1)
                    else:
                        raw = Path(imported).read_bytes() if Path(imported).stat().st_size <= LIMIT else b''
                    data = catalog(raw, minimum)
                    row = next((r for r in data['releases'] if r['platform'] == self.target), None)
                    require(row is not None)
                    save_private(cache, raw)
                    with self.lock:
                        self.release, self.path = row, None
                        self.status.update(phase='available' if version(row['version']) > version(__version__) else 'current', version=row['version'])
            except Exception:
                with self.lock:
                    self.status['phase'] = 'failed'
            finally:
                with self.lock:
                    self.busy = False

        threading.Thread(target=run, name='app-release', daemon=True).start()
        return self.state()

    def _download(self):
        row = dict(self.release)
        url = 'https://github.com/rtariq2015/cloud-stick-pro/releases/download/' + row['tag'] + '/' + row['asset']
        tmp = self.root / 'download.part'
        try:
            with opener().open(urllib.request.Request(url, headers={'User-Agent': 'CloudStickSetup-Updates'}), timeout=30) as reply, tmp.open('wb') as out:
                tmp.chmod(0o600)
                require(reply.status == 200)
                digest, done = hashlib.sha256(), 0
                with self.lock:
                    self.status['total'] = row['size']
                while chunk := reply.read(256 * 1024):
                    done += len(chunk)
                    require(done <= row['size'])
                    digest.update(chunk)
                    out.write(chunk)
                    with self.lock:
                        self.status['done'] = done
                require(done == row['size'] and digest.hexdigest() == row['sha256'])
                out.flush()
                os.fsync(out.fileno())
            destination = self.root / row['asset']
            tmp.replace(destination)
            with self.lock:
                self.path = destination
                self.status['phase'] = 'ready'
        finally:
            tmp.unlink(missing_ok=True)

    def installation(self):
        with self.lock:
            require(not self.busy and self.status['phase'] == 'ready' and self.path is not None)
            require(self.path.is_file() and not self.path.is_symlink() and self.path.stat().st_size == self.release['size'])
            with self.path.open('rb') as stream:
                require(hashlib.file_digest(stream, 'sha256').hexdigest() == self.release['sha256'])
            return self.path, dict(self.release)
