# SPDX-License-Identifier: GPL-3.0-or-later
"""Installer-only port discovery and bounded post-boot checks. No settings writes."""
import json
import re
import time
from .firmware_catalog import require


def ports():
    from serial.tools.list_ports import comports
    result = {}
    for row in comports():
        if (row.vid, row.pid) == (0x303a, 0x1001) and (row.serial_number or row.location) and re.fullmatch(r'COM[1-9][0-9]{0,3}|/dev/tty(?:ACM|USB)[0-9]{1,4}', row.device):
            result[row.device] = (row.vid, row.pid, row.serial_number, row.location)
    require(len(result) <= 64, 'firmware_device_changed')
    return result


def normal_status(port, fingerprint):
    import os
    import serial
    require(ports().get(port) == fingerprint, 'firmware_device_changed')
    channel = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=2,
                            **({'exclusive': True} if os.name == 'posix' else {}))
    channel.dtr, channel.rts, channel.port = False, False, port
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'firmware_boot_response_invalid')
            result[key] = value
        return result
    try:
        channel.open(); channel.dtr = True; time.sleep(.35); channel.reset_input_buffer()
        result = []
        for command, count in [('STATUS', 3), ('OWNER_STATUS', 1)]:
            require(ports().get(port) == fingerprint, 'firmware_device_changed')
            payload = (command + '\n').encode()
            require(channel.write(payload) == len(payload), 'firmware_boot_response_invalid')
            rows, pending, total = [], bytearray(), 0
            deadline = time.monotonic() + 6
            while len(rows) < count and time.monotonic() < deadline:
                data = channel.read(1024); pending.extend(data); total += len(data)
                require(total <= 16384 and len(pending) <= 8192, 'firmware_boot_response_invalid')
                while b'\n' in pending:
                    line, _, tail = pending.partition(b'\n'); pending = bytearray(tail)
                    if line.startswith(b'{'):
                        row = json.loads(line, object_pairs_hook=unique)
                        require(type(row) is dict, 'firmware_boot_response_invalid'); rows.append(row)
            require(len(rows) == count, 'firmware_boot_response_invalid')
            result.append(rows[0]); time.sleep(.35)
        require(ports().get(port) == fingerprint, 'firmware_device_changed')
        return result
    finally:
        channel.close()
