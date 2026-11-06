"""Prepare a fixed private owner card offline; never contacts a device.

Real output must be outside the workspace in an owner-only protected directory.
No password/salt/verifier is printed. --synthetic creates only a public test key.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import secrets
import stat
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
REFERENCE=ROOT/'phone/milestone1/test/reference/srp6a.py'
REFERENCE_SHA='9dc802e1540cce95220479cf1c56a836beeeba0869117fd3904008ac67f5537c'
USERNAME='csp-owner'
SYNTHETIC_PASSWORD='CSP-PUBLIC-SYNTHETIC-OWNER-ONLY-2026'

def reference():
    if hashlib.sha256(REFERENCE.read_bytes()).hexdigest()!=REFERENCE_SHA:
        raise ValueError('Pinned Espressif reference changed; review before using it')
    sys.path.insert(0,str(REFERENCE.parent))
    spec=importlib.util.spec_from_file_location('csp_pinned_srp6a',REFERENCE)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def create_record(mac: str, password: str):
    if len(mac)!=12 or any(c not in '0123456789abcdef' for c in mac):
        raise ValueError('Use the exact 12 lower-case hex Bluetooth MAC from OWNER_ID')
    if not password or len(password)>128 or hashlib.sha512((USERNAME+':'+password).encode()).digest()[0]==0:
        raise ValueError('Ownership input is incompatible with the pinned reference encoding')
    srp=reference()
    for _ in range(128):
        salt,verifier=srp.generate_salt_and_verifier(USERNAME,password,16)
        if len(salt)==16 and salt[0] and len(verifier)==384:break
    else:raise RuntimeError('Could not produce canonical Security 2 parameters')
    record=bytearray(432);record[:4]=b'COW2';record[4]=2
    record[6:12]=bytes.fromhex(mac);record[12:28]=salt;record[28:412]=verifier
    struct.pack_into('<I',record,428,zlib.crc32(record[:428]))
    return bytes(record)

def private_directory(path:Path, synthetic=False):
    path=path.absolute()
    if path.is_symlink() or not path.is_dir():raise ValueError('Use an existing private directory, not a symlink')
    resolved=path.resolve()
    if resolved!=path:raise ValueError('Use a private directory without symlink ancestors')
    info=path.stat()
    if info.st_uid!=os.getuid() or stat.S_IMODE(info.st_mode)!=0o700:
        raise ValueError('Output directory must belong to you and have permissions 0700')
    workspace=Path('/home/home/tool.plugin-builder')
    if not synthetic and (path==ROOT or ROOT in resolved.parents or resolved==workspace or workspace in resolved.parents):
        raise ValueError('Real ownership material must stay outside the workspace')
    return resolved

def exclusive(directory_fd:int,name:str,data:bytes):
    fd=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=directory_fd)
    try:
        with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
    except BaseException:
        # Never overwrite any prior card or recover by deleting user material.
        raise

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--private-directory',type=Path,required=True)
    parser.add_argument('--synthetic',action='store_true')
    args=parser.parse_args();directory=private_directory(args.private_directory,args.synthetic)
    password=SYNTHETIC_PASSWORD
    if not args.synthetic:
        for _ in range(128):
            password=secrets.token_urlsafe(24) # 192 independent random bits
            if hashlib.sha512((USERNAME+':'+password).encode()).digest()[0]:break
        else:raise RuntimeError('Random owner key generation failed')
    record=create_record(args.device,password)
    card={'version':2,'device':args.device,'username':USERNAME,'owner_key':password,
          'synthetic':args.synthetic,'record_sha256':hashlib.sha256(record).hexdigest(),
          'note':'Keep private. This fixed key is separate from web/Wi-Fi/Koofr passwords.'}
    # Refuse partial overwrite before writing either member of a card pair.
    if any((directory/name).exists() for name in ['owner-card.json','owner-record.bin']):
        raise ValueError('Owner card already exists; never regenerate it during reconnect/setup')
    directory_fd=os.open(directory,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:
        info=os.fstat(directory_fd)
        if info.st_uid!=os.getuid() or stat.S_IMODE(info.st_mode)!=0o700:
            raise ValueError('Private directory protection changed')
        exclusive(directory_fd,'owner-record.bin',record)
        exclusive(directory_fd,'owner-card.json',(json.dumps(card,indent=2)+'\n').encode())
        os.fsync(directory_fd)
    finally:os.close(directory_fd)
    password='';print(json.dumps({'prepared':True,'synthetic':args.synthetic,'device':args.device,
        'record_sha256':hashlib.sha256(record).hexdigest(),'device_contact':False,
        'secret_output':'private files only; not stdout'}))

if __name__=='__main__':
    try:main()
    except Exception:
        # No exception repr/local-variable rendering: card inputs are sensitive.
        print('Owner-card preparation failed. Check private path, permissions, device ID and existing files.',file=sys.stderr)
        raise SystemExit(1)
