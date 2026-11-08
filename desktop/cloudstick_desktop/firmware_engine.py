# SPDX-License-Identifier: GPL-3.0-or-later
"""Guarded ROM-only installer; orchestration is injectable for fault tests.

The board must already be in manually selected ROM download mode. This module
never ejects a disk, changes live USB ownership, toggles serial reset lines, runs
an uploaded stub, erases all flash, changes eFuses, or retries an uncertain write.
"""
import hashlib
import math
from .firmware_catalog import (APP_OFFSET, FLASH_BYTES, IMAGE_LAYOUT, PARTITION_SHA256,
                               FirmwareError, load_bundle, require)
from .firmware_recovery import CHUNK, locked


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def security(esp, expected_mac=None):
    mac = bytes(esp.read_mac()).hex()
    require(len(mac) == 12 and (expected_mac is None or mac == expected_mac), 'firmware_device_changed')
    info = esp.get_security_info(cache=False)
    require(type(info) is dict and info.get('flags') == 0 and type(info.get('flags')) is int
            and info.get('api_version') == 0 and info.get('chip_id') == 9
            and info.get('flash_crypt_cnt') == 0
            and info.get('key_purposes') == (0, 0, 0, 0, 0, 0, 12), 'firmware_security_unsupported')
    require(esp.EFUSE_MAX_KEY == 5 and tuple(esp.get_key_block_purpose(i) for i in range(6)) == (0,) * 6,
            'firmware_security_unsupported')
    require((esp.flash_id(cache=False) >> 16) & 0xff == 0x18 and esp.get_psram_cap() == 0, 'firmware_board_unsupported')
    return mac


def classify(store, catalog):
    table = store.range(0x8000, 3072)
    if sha(table) == PARTITION_SHA256:
        for app in catalog.known_apps:
            if sha(store.range(APP_OFFSET, app.size)) == app.sha256:
                return ('existing', app.version)
        raise FirmwareError('firmware_preimage_unknown')
    # Manufacturer code alone does not authorize resetting an owned board.
    # NVS must have no Cloud Stick namespaces, and the entire target journal
    # must still be erased. A missing STATUS reply is never a blank-board test.
    nvs = store.range(0x9000, 0x5000)
    require(not any(name in nvs for name in (b'csp-owner', b'csp-wifi2', b'csp\x00')), 'firmware_existing_owner')
    require(store.range(0x410000, 0x100000) == b'\xff' * 0x100000, 'firmware_existing_data')
    for stock in catalog.stock:
        if sha(store.range(0, 0x9000)) == stock.prefix_sha256 and sha(store.range(APP_OFFSET, stock.app_size)) == stock.app_sha256:
            return ('new', stock.name)
    if all(data == b'\xff' * len(data) for data in store.chunks()):
        return ('new', 'erased-original-board')
    raise FirmwareError('firmware_preimage_unknown')


def write_plan(mode, release, images, store):
    require(mode in ('existing', 'new'), 'firmware_mode_invalid')
    names = ('app.bin',) if mode == 'existing' else tuple(IMAGE_LAYOUT)
    plan = []
    for image in release.images:
        if image.name not in names:
            continue
        end = math.ceil((image.offset + image.size) / 4096) * 4096
        # Preserve bytes in the final erased sector after the new image.
        tail = store.range(image.offset + image.size, end - image.offset - image.size) if end > image.offset + image.size else b''
        plan.append((image.offset, images[image.name] + tail))
    plan.sort()
    require(all(offset % 4096 == 0 and len(data) % 4096 == 0 for offset, data in plan), 'firmware_layout_invalid')
    require(all(a + len(data) <= b for (a, data), (b, _) in zip(plan, plan[1:])), 'firmware_layout_invalid')
    return plan


def verify_all(read, original, plan, progress):
    # Compare the complete flash to the expected overlay. This includes NVS,
    # bootloader, partition table, unused bytes and all internal receipts.
    for index, before in enumerate(original.chunks()):
        offset = index * CHUNK
        expected = bytearray(before)
        for start, data in plan:
            left, right = max(offset, start), min(offset + CHUNK, start + len(data))
            if left < right:
                expected[left - offset:right - offset] = data[left - start:right - start]
        actual = read(offset, CHUNK)
        with locked(actual), locked(expected):
            require(actual == expected, 'firmware_readback_failed')
        expected[:] = b'\0' * len(expected)
        progress('verifying', offset + CHUNK, FLASH_BYTES)


def install(device, store, catalog, raw, mode, progress, mark_writing, *, resume=False):
    """No write before two reopened copies and a recognized original preimage.

    A resumed attempt uses the original sealed checkpoint and first compares all
    bytes outside the original fixed write plan. The caller captures a fresh
    independent pair as well, without replacing the original recovery copies.
    """
    images = load_bundle(raw, catalog.latest)
    detected, previous = classify(store, catalog)
    require(mode == detected, 'firmware_wrong_install_mode')
    if mode == 'existing':
        import re
        numeric = lambda value: tuple(int(n) for n in re.match(r'([0-9]+)\.([0-9]+)\.([0-9]+)', value).groups())
        require(numeric(catalog.latest.version) >= numeric(previous), 'firmware_downgrade_blocked')
    plan = write_plan(mode, catalog.latest, images, store)
    if resume:
        for index, before in enumerate(store.chunks()):
            offset = index * CHUNK
            current = bytearray(device.read(offset, CHUNK))
            for start, data in plan:
                left, right = max(offset, start), min(offset + CHUNK, start + len(data))
                if left < right:
                    current[left - offset:right - offset] = before[left - offset:right - offset]
            require(current == before, 'firmware_protected_state_changed')
            current[:] = b'\0' * len(current)
    elif mode == 'existing' and previous == catalog.latest.version:
        # Verify exact bytes rather than relying on the version label alone.
        require(sha(store.range(APP_OFFSET, catalog.latest.application.size)) == catalog.latest.application.sha256,
                'firmware_preimage_unknown')
        device.prove()
        return 'already_current'
    mark_writing()  # durable marker must be committed before the first command
    progress('writing', 0, sum(len(data) for _, data in plan))
    device.write(plan)  # exactly once; exceptions never cause an automatic retry
    verify_all(device.read, store, plan, progress)
    device.prove()
    progress('verified', 1, 1)
    return 'installed'


class ROMDevice:
    def __init__(self, port, fingerprint, enumerate_ports):
        import esptool.loader
        require(esptool.loader.WRITE_BLOCK_ATTEMPTS == 1, 'firmware_config_unsupported')
        from esptool.targets.esp32s3 import ESP32S3ROM
        from esptool.cmds import attach_flash
        import serial
        self._enumerate, self._fingerprint, self.port = enumerate_ports, fingerprint, port
        require(enumerate_ports().get(port) == fingerprint, 'firmware_device_changed')
        # Open explicitly with no asserted reset lines, before constructing the
        # vendor loader. Never let its constructor open a normal running board.
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5,
                                    **({'exclusive': True} if __import__('os').name == 'posix' else {}))
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        try:
            self.esp = ESP32S3ROM(self.serial, 115200)
            self.esp.connect(mode='no-reset', attempts=1)
            attach_flash(self.esp)
            self.mac = security(self.esp)
            self.prove()
            # ROM attach retains a small default flash geometry. Configure its
            # RAM descriptor only after proving this exact 16 MiB/no-PSRAM board;
            # otherwise READ_FLASH_SLOW stops at 2 MiB during recovery capture.
            self.esp.flash_set_parameters(FLASH_BYTES)
        except BaseException:
            self.serial.close()
            raise

    def prove(self):
        require(self._enumerate().get(self.port) == self._fingerprint, 'firmware_device_changed')
        security(self.esp, self.mac)

    def read(self, offset, size):
        require(0 <= offset <= FLASH_BYTES and 0 < size <= CHUNK and offset + size <= FLASH_BYTES, 'firmware_read_invalid')
        return bytes(self.esp.read_flash_slow(offset, size, None))

    def write(self, plan):
        from esptool.cmds import write_flash
        self.prove()
        self.esp.WRITE_FLASH_ATTEMPTS = 1
        write_flash(self.esp, plan, flash_freq='keep', flash_mode='keep', flash_size='keep', compress=False)

    def normal_boot(self):
        self.prove()
        self.esp.write_reg(self.esp.RTC_CNTL_OPTION1_REG, 0, self.esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
        self.esp.watchdog_reset()

    def close(self):
        self.serial.close()
