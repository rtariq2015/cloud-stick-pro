"""Actual owner-record CAS/NVS tests; synthetic COW2 records only, no device/network."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import zlib

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/owner-login-store'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FILES=['include/owner_login_feature.h','include/ble_feature.h','include/owner_login_nvs.h',
 'src/owner_login_nvs.cpp','lib/owner_login/owner_login_store.h','lib/owner_login/owner_login_store.cpp',
 'lib/wifi_setup_model/owner_record.h','lib/wifi_setup_model/owner_record.cpp','lib/wifi_setup_model/owner_group.h',
 'lib/wifi_setup_model/wifi_profile_store.h','lib/wifi_setup_model/wifi_setup_model.h',
 'lib/wifi_setup_model/wifi_setup_model.cpp','test/owner_login/store_stubs/nvs.h',
 'test/owner_login/store.test.cpp','test/owner_login/store_disabled.test.cpp','test/owner_login/store_tests.py']
HASHES={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in FILES}
records=[]
def publish(complete=False):
 stable=all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest for name,digest in HASHES.items())
 passed=complete and stable and all(record['passed'] for record in records)
 (OUT/'results.json').write_text(json.dumps(dict(passed=passed,complete=complete,source_stable=stable,
  desktop_only=True,device_access=False,network_access=False,real_credentials=False,
  actual_COW2_decoder=True,actual_NVS_adapter=True,actual_SHA256='host mbedTLS 2.28.8',
  target_SHA256='pinned ESP32 mbedTLS backend requires target build/physical validation',
  source_sha256=HASHES,records=records),indent=2)+'\n')
 return passed
def run(name,command):
 result=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 output=result.stdout+result.stderr
 (OUT/(name+'.log')).write_text(output)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
 passed=result.returncode==0 and not result.stderr and not diagnostic
 records.append(dict(name=name,command=command,exit=result.returncode,passed=passed,sanitizer_diagnostic=diagnostic))
 publish();print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(output[-8000:]);raise SystemExit(1)
 return result.stdout
def revision(seed):
 record=bytearray(432)
 record[:4]=b'COW2';record[4]=2;record[6:12]=bytes([0,0x11,0x22,0x33,0x44,0x55])
 record[12:28]=bytes((seed+i)&255 for i in range(16))
 record[28:412]=bytes((seed+i)&255 for i in range(384))
 record[428:432]=zlib.crc32(record[:428]).to_bytes(4,'little')
 return int.from_bytes(hashlib.sha256(record).digest()[:8],'big') or 1

clang=str(DEPS/'lib/llvm-18/bin/clang++')
common=[clang,'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
 '-Iinclude','-Ilib/owner_login','-Ilib/wifi_setup_model']
binary=str(OUT/'store-tests')
command=common+['-DCSP_ENABLE_OWNER_LOGIN=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1',
 '-DCSP_ENABLE_BLE_MANAGEMENT=1','-Itest/owner_login/store_stubs','-I'+str(DEPS/'include'),
 'lib/owner_login/owner_login_store.cpp','src/owner_login_nvs.cpp','lib/wifi_setup_model/owner_record.cpp',
 'lib/wifi_setup_model/wifi_setup_model.cpp','test/owner_login/store.test.cpp',
 '-L'+str(DEPS/'lib/x86_64-linux-gnu'),'-lmbedcrypto','-Wl,--wrap=mbedtls_sha256_ret','-o',binary]
publish();run('build-enabled',command);run('run-enabled',[binary,str(revision(1)),str(revision(2))])
disabled=binary+'-disabled'
run('build-default-disabled',common+['lib/owner_login/owner_login_store.cpp','src/owner_login_nvs.cpp',
 'test/owner_login/store_disabled.test.cpp','-o',disabled])
run('run-default-disabled',[disabled])
symbols=run('symbols-default-disabled',['nm','-C',disabled])
assert not re.search(r'\b[TtWw]\s+(?:pro::owner_login::|OwnerLoginNvs::)',symbols),'Disabled image contains owner-login implementation'
if not publish(complete=True):raise SystemExit('FAIL: source changed during tests; stable rerun required')
