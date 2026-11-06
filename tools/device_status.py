"""Bounded USB diagnostics client. Requires pyserial; never resets or ejects USB.

STATUS/LOG read device diagnostics. CHECK checks saved cloud access (and creates
the dedicated Koofr backup folder if missing). TEST also creates one synthetic
text file and verifies its contents. SETUP pauses backups and opens maintenance.
No credentials or user filenames are requested or returned by this protocol.
"""
import argparse
import json
import time
import serial


def request(port_name, command, timeout=10):
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
    # Set line state before opening, avoiding pyserial's default RTS assertion.
    port.dtr = False
    port.rts = False
    port.port = port_name
    port.open()
    try:
        port.dtr = True
        time.sleep(0.25)
        port.reset_input_buffer()
        port.write(command.upper().encode('ascii') + b'\n')
        deadline = time.monotonic() + timeout
        received = bytearray()
        while time.monotonic() < deadline:
            received.extend(port.read(1024))
            if len(received) > 8192:
                raise RuntimeError('Device response exceeded 8192 bytes')
            while b'\n' in received:
                line, _, remainder = received.partition(b'\n')
                received = bytearray(remainder)
                if line.startswith(b'{'):
                    result = json.loads(line)
                    if not isinstance(result, dict):
                        raise RuntimeError('Device returned an invalid diagnostic object')
                    return result
        raise TimeoutError('No diagnostic reply; check firmware version and USB port')
    finally:
        port.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('command', nargs='?', default='status',
                        choices=['status', 'log', 'check', 'test', 'setup'])
    args = parser.parse_args()
    print(json.dumps(request(args.port, args.command), indent=2))
