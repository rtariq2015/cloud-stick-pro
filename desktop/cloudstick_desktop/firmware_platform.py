# SPDX-License-Identifier: MIT
"""Native installer ownership and OS launch helpers; no fixed device identity."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from .firmware_catalog import require
from .firmware_recovery import private_directory


def data_root():
    base = Path(os.environ.get('LOCALAPPDATA', str(Path.home() / 'AppData/Local'))) if sys.platform == 'win32' else Path.home() / '.local/share'
    return private_directory(base / 'CloudStickSetup' / 'firmware')


@contextmanager
def operation_lock(root):
    path = root / 'serial-operation.lock'
    fd = os.open(path, os.O_RDWR | os.O_CREAT | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    legacy = None
    try:
        if sys.platform == 'win32':
            import msvcrt
            if os.fstat(fd).st_size == 0: os.write(fd, b'0')
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            old = Path.home() / '.local/share/cloud-stick-pro-recovery-vault/codex-serial-operation.lock'
            if old.exists():
                legacy = os.open(old, os.O_RDWR | os.O_NOFOLLOW)
                fcntl.flock(legacy, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        if legacy is not None: os.close(legacy)
        os.close(fd)


def ensure_rom_storage_absent(port):
    if sys.platform == 'linux':
        require(re.fullmatch(r'/dev/tty(?:ACM|USB)[0-9]{1,4}', port), 'firmware_port_invalid')
        node = (Path('/sys/class/tty') / Path(port).name / 'device').resolve(strict=True)
        usb = next((p for p in (node, *node.parents) if (p / 'idVendor').exists()), None)
        require(usb is not None and (usb / 'idVendor').read_text().strip().lower() == '303a'
                and (usb / 'idProduct').read_text().strip().lower() == '1001', 'firmware_device_changed')
        # A running CDC+MSC firmware must never be treated as ROM, including a
        # cardless drive with no mounted partition.
        require(not any(p.read_text().strip() == '08' for p in usb.glob('*/bInterfaceClass')), 'firmware_storage_attached')
        require(not any(usb in block.resolve().parents for block in Path('/sys/class/block').iterdir()), 'firmware_storage_attached')
        fuser = shutil.which('fuser')
        require(fuser is not None, 'firmware_ownership_unavailable')
        result = subprocess.run([fuser, port], capture_output=True, timeout=3)
        require(result.returncode == 1 and not result.stdout.strip() and not result.stderr.strip(), 'firmware_serial_busy')
    elif sys.platform == 'win32':
        require(re.fullmatch(r'COM[1-9][0-9]{0,3}', port), 'firmware_port_invalid')
        # Inspect the selected composite USB ancestor and its connected disks.
        # Bounded COM syntax above is the only interpolated value.
        script = r'''
$ErrorActionPreference='Stop'
function Parent($id) { (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Parent -ErrorAction Stop).Data }
function Root($id) {
 for($i=0;$i -lt 12;$i++) {
  if($id -match '^USB\\VID_303A&PID_1001\\[^\\]+$') { return $id }
  if($id -match '^HTREE\\ROOT' -or $id -eq '') { return '' }
  $id=Parent $id
 }
 return ''
}
$ports=@(Get-PnpDevice -PresentOnly -Class Ports | Where-Object { $_.FriendlyName -match '\(PORTVALUE\)$' })
if($ports.Count -ne 1) { exit 2 }
$root=Root $ports[0].InstanceId
if(-not $root) { exit 3 }
foreach($disk in @(Get-PnpDevice -PresentOnly -Class DiskDrive)) {
 try { if((Root $disk.InstanceId) -eq $root) { exit 4 } } catch { exit 5 }
}
[Console]::Write('safe')
'''.replace('PORTVALUE', port)
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], capture_output=True, timeout=30)
        require(result.returncode == 0 and result.stdout.strip() == b'safe', 'firmware_storage_check_failed')
    else:
        require(False, 'unsupported_platform')


def worker_command(job):
    if getattr(sys, 'frozen', False):
        helper = Path(sys.executable).parent / 'firmware-helper' / ('cloud-stick-installer.exe' if sys.platform == 'win32' else 'cloud-stick-installer')
        require(helper.is_file(), 'firmware_worker_start_failed')
        return [str(helper), '--firmware-worker', str(job)]
    return [sys.executable, '-m', 'cloudstick_desktop', '--firmware-worker', str(job)]


def launch_worker(job, unit):
    command = worker_command(job)
    env = dict(os.environ)
    env['PYTHONPATH'] = str(Path(__file__).resolve().parents[1])
    if sys.platform == 'linux':
        for name in ('systemd-run', 'systemd-inhibit'):
            require(shutil.which(name) is not None, 'private_memory_unavailable')
        command = ['systemd-run', '--user', '--quiet', '--collect', '--unit=' + unit,
                   '--property=MemorySwapMax=0', '--property=LimitCORE=0',
                   '--property=KillMode=process', '--property=TimeoutStopSec=infinity',
                   'systemd-inhibit', '--what=sleep', '--mode=block', '--who=Cloud Stick Setup',
                   '--why=Firmware installation and recovery', *command]
        result = subprocess.run(command, env=env, capture_output=True, timeout=15)
        require(result.returncode == 0, 'firmware_worker_start_failed')
        return None
    flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
    return subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, close_fds=True, creationflags=flags).pid


def worker_alive(job):
    if sys.platform == 'linux':
        result = subprocess.run(['systemctl', '--user', 'is-active', job['unit']], capture_output=True, timeout=3)
        return result.stdout.strip() in (b'active', b'activating', b'deactivating')
    pid = job.get('pid')
    if type(pid) is not int: return False
    import ctypes
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes, k.OpenProcess.restype = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32], ctypes.c_void_p
    k.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    k.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = k.OpenProcess(0x1000, False, pid)
    if not handle: return False
    code = ctypes.c_uint32()
    try: return bool(k.GetExitCodeProcess(handle, __import__('ctypes').byref(code))) and code.value == 259
    finally: k.CloseHandle(handle)
