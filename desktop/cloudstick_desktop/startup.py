# SPDX-License-Identifier: MIT
"""Opt-in per-user USB watcher. Enumerates metadata; never opens a serial port."""
import os
import json
from pathlib import Path
import subprocess
import sys
import time


def executable():
    if not getattr(sys,'frozen',False): raise ValueError('installed_app_required')
    return str(Path(sys.executable).resolve())


def location():
    return Path.home()/'.config/autostart/cloud-stick-desktop.desktop'


def enabled():
    if sys.platform=='win32':
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\Microsoft\Windows\CurrentVersion\Run') as key:
                return winreg.QueryValueEx(key,'Cloud Stick Setup')[0]==subprocess.list2cmdline([executable(),'--watch'])
        except FileNotFoundError:return False
    return location().is_file()


def configure(enable):
    command=executable()
    if sys.platform=='win32':
        import winreg
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\Microsoft\Windows\CurrentVersion\Run') as key:
            if enable:winreg.SetValueEx(key,'Cloud Stick Setup',0,winreg.REG_SZ,subprocess.list2cmdline([command,'--watch']))
            else:
                try:winreg.DeleteValue(key,'Cloud Stick Setup')
                except FileNotFoundError:pass
    else:
        path=location()
        if enable:
            quoted='"'+command.replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')+'"'
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('[Desktop Entry]\nType=Application\nName=Cloud Stick Setup\nExec='+quoted+' --watch\nTerminal=false\nX-GNOME-Autostart-enabled=true\n')
        else:path.unlink(missing_ok=True)


def pause_watcher():
    """Release mapped program files before an approved app replacement."""
    from PySide6.QtCore import QLockFile
    from .firmware_platform import data_root
    root=data_root()
    token=process_token(os.getpid())
    if token is None:raise ValueError('process_identity_unavailable')
    from .firmware_recovery import save_private
    save_private(root/'watcher-paused',json.dumps({'pid':os.getpid(),'token':token}).encode())
    lock=QLockFile(str(root/'usb-watcher.lock'));lock.setStaleLockTime(0)
    for _ in range(60):
        if lock.tryLock(0):
            lock.unlock()
            return
        time.sleep(.1)
    raise ValueError('watcher_still_running')


def process_token(pid):
    if sys.platform=='win32':
        from .firmware_platform import process_identity
        return process_identity(pid)
    try:
        return (Path('/proc')/str(pid)/'stat').read_text().rsplit(')',1)[1].split()[19]
    except (OSError,IndexError):return None


def paused(root):
    flag=root/'watcher-paused'
    if not flag.exists():return False
    try:
        state=json.loads(flag.read_text())
        if type(state.get('pid')) is int and state['pid']>1 and state.get('token') is not None and process_token(state['pid'])==state['token']:
            return True
    except (ValueError,OSError):pass
    flag.unlink(missing_ok=True)
    return False


def resume_watcher():
    if not getattr(sys,'frozen',False) or not enabled():return
    from .firmware_platform import data_root
    from .app_install import clean_environment
    (data_root()/'watcher-paused').unlink(missing_ok=True)
    subprocess.Popen([executable(),'--watch'],env=clean_environment(),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)


def watch():
    from PySide6.QtCore import QLockFile
    from .firmware_platform import data_root
    from .firmware_recovery import private_directory
    from .transports import NativeUSB
    from .app_install import clean_environment
    root=data_root();private_directory(root)
    lock=QLockFile(str(root/'usb-watcher.lock'));lock.setStaleLockTime(0)
    if not lock.tryLock(0):return 0
    previous=set();usb=NativeUSB();children=[]
    while enabled() and not paused(root):
        try:
            children[:]=[p for p in children if p.poll() is None]
            current=set(usb.ports())
            if current-previous:
                children.append(subprocess.Popen([executable()],env=clean_environment(),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
            previous=current
        except OSError:pass
        time.sleep(4)
    return 0
