# SPDX-License-Identifier: MIT
"""Pause and reap the existing stick's worker before guided physical removal."""
import time
from .diagnostics import DiagnosticsChannel
from .firmware_catalog import require


def board_serial(fingerprint):
    import re
    serial = fingerprint[2]
    require(type(serial) is str, 'firmware_identity_unavailable')
    value = re.sub('[:-]', '', serial).lower()
    require(re.fullmatch('[0-9a-f]{12}', value), 'firmware_identity_unavailable')
    return value


def prepare(usb, port, fingerprint, stop):
    status = usb.exchange(port, fingerprint, 'STATUS', stop)
    require(status['owner_ready'] is True and status['store_fault'] is False and status['ble'] is False,
            'firmware_close_setup_first')
    require(usb.exchange(port, fingerprint, 'BENCH_WINDOW', stop) == {'accepted': True}, 'firmware_pause_failed')
    stable = 0
    deadline = time.monotonic() + 18
    while time.monotonic() < deadline:
        snapshot = usb.diagnostics(port, fingerprint, stop)
        backup = snapshot['backup']
        ready = (snapshot['status']['backups_paused'] is True and snapshot['status']['ble'] is True
                 and backup['worker_phase'] == 0 and backup['worker_error'] == 0
                 and backup['active'] is False and backup['paused'] is True)
        stable = stable + 1 if ready else 0
        if stable >= 2:
            break
        require(not stop.wait(.5), 'cancelled')
    require(stable >= 2, 'firmware_pause_failed')
    # Private extension uses the same paced, exact-response channel. It cannot
    # become a renderer-selected arbitrary serial command.
    with usb._session(port, fingerprint, stop) as serial_port:
        class Channel(DiagnosticsChannel):
            COMMANDS = frozenset({'POWER_POLICY_STATUS'})
        channel = Channel(serial_port, stop, lambda: usb.ports().get(port) == fingerprint, usb._json)
        row = channel.request('POWER_POLICY_STATUS')
        require(type(row) is dict and row.get('owned') is False and row.get('fault') == 0
                and row.get('sdk_error') == 0 and row.get('previous') == -1, 'firmware_pause_failed')
    return {'board_serial': board_serial(fingerprint), 'revision': status['saved_revision'], 'owner_ready': True}
