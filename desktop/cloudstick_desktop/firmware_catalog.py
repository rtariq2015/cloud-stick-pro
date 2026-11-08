# SPDX-License-Identifier: MIT
"""Authenticated firmware metadata and bounded packages. No serial imports.

Only the project release key authorizes image identities. A downloaded manifest's
own signature assertion, a local file, or renderer-supplied URL grants nothing.
"""
from dataclasses import dataclass
import base64
import hashlib
import io
import json
from pathlib import Path
import re
import ssl
import urllib.parse
import urllib.request
import zipfile

BOARD = 'lilygo-tdongle-s3-original-16m-nopsram'
FLASH_BYTES = 0x1000000
APP_OFFSET, APP_CAPACITY = 0x10000, 0x400000
PARTITION_SHA256 = '35897a52e0d951707f219eb6292b945ad55095364e3a8075eeeb4b221e211798'
CATALOG_URL = 'https://raw.githubusercontent.com/rtariq2015/cloud-stick-pro/main/desktop/firmware/catalog.json'
PUBLIC_KEY = bytes.fromhex('37eda4bd2401b1883fcf75b99c134e52b9e7294d9046089ccfc0bd9d6dfcd6df')
KEY_ID = 'csp-release-ed25519-1'
CATALOG_LIMIT, BUNDLE_LIMIT = 256 * 1024, 8 * 1024 * 1024
IMAGE_LAYOUT = {'bootloader.bin': (0, 0x8000), 'partitions.bin': (0x8000, 3072),
                'nvs-empty.bin': (0x9000, 0x5000), 'otadata-empty.bin': (0xe000, 0x2000),
                'app.bin': (APP_OFFSET, APP_CAPACITY), 'journal.bin': (0x410000, 0x100000)}
VERSION = re.compile(r'[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}(?:-[a-z0-9-]+(?:\.[a-z0-9-]+)*)?')
SHA = re.compile('[0-9a-f]{64}')


class FirmwareError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def require(condition, code='firmware_metadata_invalid'):
    if not condition:
        raise FirmwareError(code)


def exact(value, keys):
    require(type(value) is dict and set(value) == set(keys))


def bounded_int(value, low, high):
    require(type(value) is int and low <= value <= high)
    return value


def digest(value):
    require(type(value) is str and SHA.fullmatch(value) is not None)
    return value


def version(value):
    require(type(value) is str and len(value) <= 36 and VERSION.fullmatch(value) is not None)
    return value


def strict_json(raw, limit=CATALOG_LIMIT):
    require(type(raw) is bytes and len(raw) <= limit)
    def pairs(rows):
        result = {}
        for key, value in rows:
            require(key not in result)
            result[key] = value
        return result
    try:
        return json.loads(raw.decode('utf-8'), object_pairs_hook=pairs)
    except (ValueError, UnicodeError, RecursionError):
        raise FirmwareError('firmware_metadata_invalid') from None


@dataclass(frozen=True)
class Image:
    name: str
    offset: int
    size: int
    sha256: str


@dataclass(frozen=True)
class KnownApp:
    version: str
    size: int
    sha256: str


@dataclass(frozen=True)
class Stock:
    name: str
    prefix_sha256: str
    app_size: int
    app_sha256: str
    source: str


@dataclass(frozen=True)
class Release:
    version: str
    source: str
    tag: str
    asset: str
    bundle_size: int
    bundle_sha256: str
    images: tuple[Image, ...]

    @property
    def url(self):
        return 'https://github.com/rtariq2015/cloud-stick-pro/releases/download/' + self.tag + '/' + self.asset

    @property
    def application(self):
        return next(image for image in self.images if image.name == 'app.bin')


@dataclass(frozen=True)
class Catalog:
    revision: int
    releases: tuple[Release, ...]
    known_apps: tuple[KnownApp, ...]
    stock: tuple[Stock, ...]
    signed_sha256: str

    @property
    def latest(self):
        return self.releases[0]


def load_catalog(raw, *, public_key=PUBLIC_KEY, minimum_revision=1):
    envelope = strict_json(raw)
    exact(envelope, ('schema', 'key_id', 'payload', 'signature'))
    require(envelope['schema'] == 'cloud-stick-signed-catalog/v1' and envelope['key_id'] == KEY_ID)
    try:
        require(type(envelope['payload']) is str and type(envelope['signature']) is str)
        payload = base64.b64decode(envelope['payload'], validate=True)
        signature = base64.b64decode(envelope['signature'], validate=True)
        require(len(payload) <= 160 * 1024 and len(signature) == 64)
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        Ed25519PublicKey.from_public_bytes(public_key).verify(signature, payload)
    except FirmwareError:
        raise
    except Exception:
        raise FirmwareError('firmware_signature_invalid') from None
    data = strict_json(payload)
    require(type(data) is dict)
    require(data.get('schema') == 'cloud-stick-firmware-catalog/v1', 'firmware_app_update_required')
    exact(data, ('schema', 'board', 'revision', 'releases', 'known_apps', 'stock'))
    require(data['schema'] == 'cloud-stick-firmware-catalog/v1' and data['board'] == BOARD)
    revision = bounded_int(data['revision'], minimum_revision, 2**31 - 1)
    require(type(data['releases']) is list and 1 <= len(data['releases']) <= 16)
    releases = []
    for row in data['releases']:
        exact(row, ('version', 'source', 'tag', 'asset', 'bundle_bytes', 'bundle_sha256', 'images'))
        require(type(row['source']) is str and re.fullmatch('[0-9a-f]{40}', row['source']))
        require(type(row['tag']) is str and re.fullmatch('[a-zA-Z0-9][a-zA-Z0-9._-]{0,95}', row['tag']))
        require(type(row['asset']) is str and re.fullmatch(r'cloud-stick-firmware-[a-z0-9._-]{1,80}\.cspfw', row['asset']))
        require(type(row['images']) is list and len(row['images']) == len(IMAGE_LAYOUT))
        images = []
        for entry in row['images']:
            exact(entry, ('name', 'offset', 'bytes', 'sha256'))
            require(type(entry['name']) is str and entry['name'] in IMAGE_LAYOUT)
            offset, maximum = IMAGE_LAYOUT[entry['name']]
            require(type(entry['offset']) is int and entry['offset'] == offset)
            size = bounded_int(entry['bytes'], 32, maximum)
            if entry['name'] not in ('bootloader.bin', 'app.bin'):
                require(size == maximum)
            images.append(Image(entry['name'], offset, size, digest(entry['sha256'])))
        require({image.name for image in images} == set(IMAGE_LAYOUT))
        require(next(image for image in images if image.name == 'partitions.bin').sha256 == PARTITION_SHA256)
        releases.append(Release(version(row['version']), row['source'], row['tag'], row['asset'],
                                bounded_int(row['bundle_bytes'], 1024, BUNDLE_LIMIT), digest(row['bundle_sha256']), tuple(images)))
    require(len({r.version for r in releases}) == len(releases))
    require(type(data['known_apps']) is list and 1 <= len(data['known_apps']) <= 32)
    known = []
    for row in data['known_apps']:
        exact(row, ('version', 'bytes', 'sha256'))
        known.append(KnownApp(version(row['version']), bounded_int(row['bytes'], 32, APP_CAPACITY), digest(row['sha256'])))
    require(len({app.sha256 for app in known}) == len(known))
    require(all(any(app.sha256 == r.application.sha256 and app.size == r.application.size and app.version == r.version for app in known) for r in releases))
    require(type(data['stock']) is list and len(data['stock']) <= 8)
    stock = []
    for row in data['stock']:
        exact(row, ('name', 'prefix_sha256', 'app_bytes', 'app_sha256', 'source'))
        require(type(row['name']) is str and re.fullmatch('[a-z0-9._-]{1,80}', row['name']))
        require(type(row['source']) is str and re.fullmatch(r'https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/[0-9a-f]{40}/firmware/[A-Za-z0-9._-]+\.bin', row['source']))
        stock.append(Stock(row['name'], digest(row['prefix_sha256']), bounded_int(row['app_bytes'], 32, APP_CAPACITY), digest(row['app_sha256']), row['source']))
    return Catalog(revision, tuple(releases), tuple(known), tuple(stock), hashlib.sha256(raw).hexdigest())


def load_bundle(raw, release):
    require(type(raw) is bytes and len(raw) == release.bundle_size and hashlib.sha256(raw).hexdigest() == release.bundle_sha256, 'firmware_package_invalid')
    expected = {image.name: image for image in release.images}
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            require(len(entries) == len(expected) and {entry.filename for entry in entries} == set(expected), 'firmware_package_invalid')
            result = {}
            for entry in entries:
                image = expected[entry.filename]
                require(entry.file_size == image.size and entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) and not entry.flag_bits & 1, 'firmware_package_invalid')
                require(entry.external_attr >> 28 != 0xa, 'firmware_package_invalid')
                data = archive.read(entry)
                require(len(data) == image.size and hashlib.sha256(data).hexdigest() == image.sha256, 'firmware_package_invalid')
                result[image.name] = data
    except FirmwareError:
        raise
    except Exception:
        raise FirmwareError('firmware_package_invalid') from None
    require(result['nvs-empty.bin'] == b'\xff' * 0x5000 and result['otadata-empty.bin'] == b'\xff' * 0x2000, 'firmware_package_invalid')
    return result


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        parsed = urllib.parse.urlsplit(newurl)
        require(parsed.scheme == 'https' and parsed.hostname in ('github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com') and parsed.port in (None, 443) and not parsed.username and not parsed.password and not parsed.fragment, 'firmware_download_failed')
        return super().redirect_request(request, fp, code, message, headers, newurl)


def download(url, limit, *, catalog=False):
    if catalog:
        require(url == CATALOG_URL and limit == CATALOG_LIMIT, 'firmware_download_failed')
    else:
        require(type(url) is str and re.fullmatch(r'https://github.com/rtariq2015/cloud-stick-pro/releases/download/[A-Za-z0-9][A-Za-z0-9._-]{0,95}/cloud-stick-firmware-[a-z0-9._-]{1,80}\.cspfw', url), 'firmware_download_failed')
        require(type(limit) is int and 1024 <= limit <= BUNDLE_LIMIT, 'firmware_download_failed')
    try:
        opener = urllib.request.build_opener(SafeRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        request = urllib.request.Request(url, headers={'User-Agent': 'CloudStickSetup-Firmware/0.6.1', 'Accept': 'application/octet-stream'})
        with opener.open(request, timeout=20) as response:
            require(response.status == 200, 'firmware_download_failed')
            raw = response.read(limit + 1)
        require(len(raw) <= limit, 'firmware_download_failed')
        return raw
    except FirmwareError:
        raise
    except Exception:
        raise FirmwareError('firmware_download_failed') from None
