"""Compile pinned IDF v5.5.5 Security2/SRP with reviewed alignment correction.

Requires libmbedtls-dev/libprotobuf-c-dev and the pinned installed SDK headers.
This is not an emulation of BLE, RTOS scheduling, ESP hardware crypto or device RAM.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

parser=argparse.ArgumentParser();parser.add_argument('--sanitize',action='store_true');parser.add_argument('--rng-lifetime',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
test=root/'test/milestone1';out=root/'artifacts/milestone1/host-security2';out.mkdir(parents=True,exist_ok=True)
provenance=json.loads((test/'sdk_vendor/PROVENANCE.json').read_text())
for name,entry in provenance['files'].items():
 data=(test/'sdk_vendor'/name).read_bytes()
 actual=hashlib.sha1(('blob '+str(len(data))+'\0').encode()+data).hexdigest()
 if actual!=entry['git_blob_sha']:raise SystemExit('Unreviewed SDK source change: '+name)
spec=importlib.util.spec_from_file_location('pinned_ble_sdk',root/'tools/pinned_ble_sdk.py')
pinned=importlib.util.module_from_spec(spec);spec.loader.exec_module(pinned)
pinned.verify()
# Use exactly the firmware's public synthetic M1 fixture. Randomly generated salts
# can exercise encodings deliberately outside this narrowly supported prototype.
# Extract only the two source arrays and verify their public known fingerprints;
# never read NVS or any device/private credential material.
firmware_source=(root/'src/ble_management.cpp').read_text()
expected={'sec2_salt':(16,'1a4ed7542c054585806014add396f6dd8cd20c25e665e2ea40c8ba25a5894c2d'),
          'sec2_verifier':(384,'e985f1fb10e7c5aaf625ec7cc0896e8f92bdcd5b621141766ea6ea93bd899b06')}
header=['/* Generated from public synthetic firmware fixture; not device credentials. */','#include <stdint.h>']
fixture={}
for name,(length,digest) in expected.items():
 match=re.search(r'static const uint8_t '+name+r'\[\]\s*=\s*\{(.*?)\};',firmware_source,re.S)
 if not match:raise SystemExit('Missing public synthetic fixture: '+name)
 value=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',match.group(1)))
 if len(value)!=length or hashlib.sha256(value).hexdigest()!=digest:raise SystemExit('Unreviewed synthetic fixture change: '+name)
 header.append('static const uint8_t host_'+name+'[] = {'+','.join('0x%02x'%b for b in value)+'};')
 fixture[name]={'bytes':length,'sha256':digest}
(out/'synthetic_fixture.h').write_text('\n'.join(header)+'\n')
(out/'synthetic-fixture.json').write_text(json.dumps({'source':'src/ble_management.cpp','synthetic_only':True,'matches_firmware_arrays':True,'arrays':fixture},indent=2)+'\n')
sdk=Path(os.environ.get('PLATFORMIO_CORE_DIR','/config/.unmanic/research/cloud-stick-platformio'))/'packages/framework-arduinoespressif32-libs/esp32s3/include/protocomm'
includes=[test/'host_shims',test/'sdk_vendor',out,sdk/'include/security',sdk/'include/crypto/srp6a',sdk/'proto-c']
binary=out/('security2-host-asan' if args.sanitize else 'security2-host')
if args.rng_lifetime: binary=out/('security2-host-rng-asan' if args.sanitize else 'security2-host-rng')
cmd=['gcc','-std=gnu11','-g','-O1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-include',str(test/'host_shims/mbedtls_compat.h')]
if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
for inc in includes:cmd+=['-I',str(inc)]
cmd += [str(test/'security2_host.c'),str(root/'sdk/pinned_ble/patched/security2.c')]+[str(test/'sdk_vendor'/name) for name in ['esp_srp.c','esp_srp_mpi.c','session.pb-c.c','sec0.pb-c.c','sec1.pb-c.c','sec2.pb-c.c','constants.pb-c.c']]
if args.rng_lifetime:
 cmd+=['-DCSP_TEST_RNG_LIFETIME=1',str(test/'rng_lifetime.c')]
 for name in ['mbedtls_entropy_init','mbedtls_entropy_free','mbedtls_ctr_drbg_init','mbedtls_ctr_drbg_free','mbedtls_ctr_drbg_seed','mbedtls_ctr_drbg_random']:
  cmd+=['-Wl,--wrap='+name]
cmd+=['-lmbedtls','-lmbedcrypto','-lmbedx509','-lprotobuf-c','-o',str(binary)]
subprocess.run(cmd,check=True)
(out/'build.json').write_text(json.dumps({'command':cmd,'binary':str(binary),'sanitized':args.sanitize,'strict_no_recovery':args.sanitize,'alignment_patch':'sdk/pinned_ble/security2.c.patch','same_security2_source_as_firmware':True,'radio_or_device_access':False,'caveat':'Pinned SDK with reviewed byte-counter alignment correction and host mbedTLS2.28 compatibility aliases; not ESP32 binary or RAM test'},indent=2)+'\n')
print(binary)
