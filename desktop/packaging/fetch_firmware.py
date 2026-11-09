# SPDX-License-Identifier: MIT
"""Fetch only the exact signed firmware bundle selected by the reviewed catalog."""
from pathlib import Path
import os
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from cloudstick_desktop.firmware_catalog import load_catalog,load_bundle,download,FirmwareError


def fetch(root):
    root=Path(root);catalog=load_catalog((root/'catalog.json').read_bytes());release=catalog.latest
    destination=root/release.asset
    if destination.is_file():
        try:
            load_bundle(destination.read_bytes(),release)
            return destination
        except FirmwareError:
            pass
    package=download(release.url,release.bundle_size)
    load_bundle(package,release)
    descriptor,name=tempfile.mkstemp(prefix='.verified-firmware-',dir=root)
    temporary=Path(name)
    try:
        with os.fdopen(descriptor,'wb') as stream:
            stream.write(package);stream.flush();os.fsync(stream.fileno())
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


if __name__=='__main__':
    target=fetch(Path(sys.argv[1]));print('Verified signed firmware package: '+target.name)
