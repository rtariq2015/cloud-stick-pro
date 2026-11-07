"""Build reviewed six-image installer data; private signing key never enters output.

Inputs are explicit public images. Run after the guarded firmware build and native
LittleFS compatibility check. This tool never discovers or contacts hardware.
"""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import sys
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'desktop'))
from cloudstick_desktop.firmware_catalog import (BOARD, IMAGE_LAYOUT, KEY_ID, PUBLIC_KEY,
                                                PARTITION_SHA256, load_bundle, load_catalog)

APP_SHA = 'b0d5984282d5ed41f2dfa99558aca38aae8b865102d959de3ff61118228554a3'
SOURCE = 'adc439bcc69802d289fa79b34f9994f4093d0f83'
VERSION = '0.4.6-cache-dev'
KNOWN = [
    ('0.4.2-incremental-dev', 1689280, 'd0aa666e7fe0e4f47f7bef851bda82415aad10e895779c739eb1bb7ad02191b2'),
    ('0.4.3-ui-dev', 1689744, '1b7c24ebd4b9de0750f46f3875c230469533b38ab0fd8234bb14d914361a994e'),
    ('0.4.4-wifi-dev', 1689808, 'b3a19ed7e00a0dd68b30b08c5efbac061a33089dc5256a8eae57068dd46bb579'),
    ('0.4.5-ble-dev', 1689840, 'd9579044bf9df00e2a80149f89162a486dfbef7ba30cf8ea5f01c719ee94734c'),
    (VERSION, 1691232, APP_SHA),
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(app, bootloader, partitions, journal, stock, tag):
    if len(app) != 1691232 or sha(app) != APP_SHA or sha(partitions) != PARTITION_SHA256:
        raise ValueError('Only the reviewed normal 0.4.6 build and partition table are admitted')
    if len(bootloader) != 19968 or sha(bootloader) != '84bb365666003b051f564d04380d0cf5d9d85c420ddafe9fc863071b49e49bfc' or bootloader[0] != 0xe9 or app[0] != 0xe9:
        raise ValueError('Invalid original-board boot image')
    if len(journal) != 0x100000 or sha(journal) != '2853d81054fb721d2ecef4dd9c5b6185f1c9b9163605528d822afdad7a1c95f4':
        raise ValueError('Use the reviewed empty /queue LittleFS image')
    if len(stock) != 1378288 or sha(stock) != '65a9b51487ca721f618a0f22bb4d800f0dedfcb40c29286f55ad85eba81c68cd':
        raise ValueError('Use the reviewed original-board manufacturer image for recognition only')
    files = {'app.bin': app, 'bootloader.bin': bootloader, 'partitions.bin': partitions,
             'journal.bin': journal, 'nvs-empty.bin': b'\xff' * 0x5000, 'otadata-empty.bin': b'\xff' * 0x2000}
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 7, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, files[name], compresslevel=9)
    raw = buffer.getvalue()
    asset = 'cloud-stick-firmware-0.4.6-cache-dev.cspfw'
    release = {'version': VERSION, 'source': SOURCE, 'tag': tag, 'asset': asset,
               'bundle_bytes': len(raw), 'bundle_sha256': sha(raw), 'images': [
                   {'name': name, 'offset': IMAGE_LAYOUT[name][0], 'bytes': len(files[name]), 'sha256': sha(files[name])}
                   for name in IMAGE_LAYOUT]}
    payload = {'schema': 'cloud-stick-firmware-catalog/v1', 'board': BOARD, 'revision': 1,
               'releases': [release], 'known_apps': [{'version': v, 'bytes': n, 'sha256': h} for v, n, h in KNOWN],
               'stock': [{'name': 'lilygo-original-factory-screen-251218',
                          'prefix_sha256': sha(stock[:0x9000]), 'app_bytes': len(stock) - 0x10000,
                          'app_sha256': sha(stock[0x10000:]),
                          'source': 'https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/03d51691e70271aeb988d0d1d930650595176736/firmware/T-Dongle-S3_factory_screen_251218.bin'}]}
    return raw, payload


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('app', 'bootloader', 'partitions', 'journal', 'stock', 'key', 'output'):
        p.add_argument('--' + name, required=True, type=Path)
    p.add_argument('--tag', required=True)
    a = p.parse_args()
    raw, payload = build(*(getattr(a, n).read_bytes() for n in ('app', 'bootloader', 'partitions', 'journal', 'stock')), a.tag)
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    key = serialization.load_pem_private_key(a.key.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey) or key.public_key().public_bytes_raw() != PUBLIC_KEY:
        raise ValueError('Wrong release key')
    body = json.dumps(payload, separators=(',', ':'), sort_keys=True).encode()
    envelope = json.dumps({'schema': 'cloud-stick-signed-catalog/v1', 'key_id': KEY_ID,
                           'payload': base64.b64encode(body).decode(),
                           'signature': base64.b64encode(key.sign(body)).decode()}, indent=2).encode() + b'\n'
    catalog = load_catalog(envelope)
    load_bundle(raw, catalog.latest)
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / catalog.latest.asset).write_bytes(raw)
    (a.output / 'catalog.json').write_bytes(envelope)
    (a.output / 'bundle-provenance.json').write_text(json.dumps({'schema': 'cloud-stick-install-bundle/v1',
        'firmware_source': SOURCE, 'firmware_version': VERSION, 'catalog_sha256': sha(envelope),
        'bundle_sha256': sha(raw), 'bundle_bytes': len(raw), 'images': payload['releases'][0]['images'],
        'factory_hardware_acceptance': 'pending-unconfigured-original-board',
        'vendor_binary_included': False}, indent=2) + '\n')
    print('Verified public install bundle and signed catalog created; no hardware contacted')


if __name__ == '__main__':
    main()
