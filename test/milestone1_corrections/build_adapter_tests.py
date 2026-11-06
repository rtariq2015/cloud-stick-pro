"""Strict host-only tests of the actual adapter and pinned corrected SDK C.

Clock/radio/lifecycle stubs cannot establish target scheduling, RF, USB or RAM.
"""
from pathlib import Path
import hashlib, importlib.util, json, subprocess
root=Path(__file__).resolve().parents[2]
test=root/'test/milestone1'
source=root/'test/milestone1_corrections'
out=root/'artifacts/milestone1-corrections/adapter';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(root/'tools/pinned_ble_sdk.py')],check=True)
sdk=Path('/config/.unmanic/research/cloud-stick-platformio/packages/framework-arduinoespressif32-libs/esp32s3/include/protocomm')
includes=[test/'host_shims',test/'sdk_vendor',sdk/'include/security',sdk/'include/crypto/srp6a',sdk/'proto-c']
common=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
commands=[];objects=[]
for name in ['security2.c','esp_srp.c','esp_srp_mpi.c','session.pb-c.c','sec0.pb-c.c','sec1.pb-c.c','sec2.pb-c.c','constants.pb-c.c']:
 obj=out/(name+'.o');objects.append(str(obj))
 vendor=root/'sdk/pinned_ble/patched/security2.c' if name=='security2.c' else test/'sdk_vendor'/name
 cmd=['gcc','-std=gnu11',*common,'-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-include',str(test/'host_shims/mbedtls_compat.h')]
 for inc in includes:cmd+=['-I',str(inc)]
 cmd+=['-c',str(vendor),'-o',str(obj)]
 commands.append(cmd);subprocess.run(cmd,check=True)
defines=['-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_BLE_SYNTHETIC_ONLY=1','-DCSP_BLE_ISOLATED_TEST=1','-DCONFIG_BT_NIMBLE_ENABLED=1',
 '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
for name,stubs,extra in [('actual_adapter_sdk.cpp','sdk_adapter_stubs',objects+['-lmbedtls','-lmbedcrypto','-lmbedx509','-lprotobuf-c']),
                         ('adapter_timer_interleavings.cpp','adapter_stubs',[]),
                         ('adapter_callback_boundaries.cpp','adapter_stubs',[])]:
 binary={'actual_adapter_sdk.cpp':'actual-adapter-sdk-asan','adapter_timer_interleavings.cpp':'timer-tests','adapter_callback_boundaries.cpp':'callback-tests'}[name]
 cmd=['g++','-std=c++17','-pthread','-Wall','-Wextra','-Werror',*common,*defines,
   '-I',str(source/stubs),'-I',str(root/'include'),str(source/name),*extra,'-o',str(out/binary)]
 commands.append(cmd);subprocess.run(cmd,check=True)
(out/'build.json').write_text(json.dumps({'commands':commands,'adapter_sha256':hashlib.sha256((root/'src/ble_management.cpp').read_bytes()).hexdigest(),
 'actual_adapter':True,'sdk':'pinned v5.5.5 with reviewed byte-safe IV correction; pinned_ble_sdk.py verification enforced',
 'sanitizers':'ASan+UBSan, fail on diagnostics in runners','stubs':'clock, MAC, heap, radio/protocomm lifecycle only','device_access':False},indent=2)+'\n')
print(out)
