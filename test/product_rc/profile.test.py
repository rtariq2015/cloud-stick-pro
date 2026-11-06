"""Compile actual feature headers; no device, persistence or cloud access."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
wifi=['CSP_ENABLE_BLE_MANAGEMENT=1','CSP_ENABLE_WIFI_SETUP=1']
base=wifi+['CSP_ENABLE_BACKUP_SETUP=1','CSP_USB_BENCH_AUTOMATION=1']
rc=base+['CSP_PRODUCT_RC_PROFILE=1','CSP_NIMBLE_STOP_REPAIR=1']
cases=[('default',[],True),('ordinary-rc',rc,True),
       ('legacy-lab5',base+['CSP_PRODUCT_LAB5_PROFILE=1','CSP_NIMBLE_STOP_REPAIR=1'],True),
       ('missing-combined',wifi+['CSP_PRODUCT_RC_PROFILE=1','CSP_NIMBLE_STOP_REPAIR=1'],False),
       ('missing-repair',base+['CSP_PRODUCT_RC_PROFILE=1'],False),
       ('fixture-not-production',rc+['CSP_BACKUP_FIXTURE_ONLY=1'],False),
       ('bad-rc-value',base+['CSP_PRODUCT_RC_PROFILE=2','CSP_NIMBLE_STOP_REPAIR=1'],False),
       ('public-proof-denied',rc+['CSP_BLE_SYNTHETIC_ONLY=1'],False)]
for flag in ['CSP_PRODUCT_LAB_PROFILE','CSP_PRODUCT_LAB2_PROFILE','CSP_PRODUCT_LAB3_PROFILE',
             'CSP_PRODUCT_LAB4_PROFILE','CSP_PRODUCT_LAB5_PROFILE','CSP_PRODUCT_LAB6_PROFILE',
             'CSP_PRODUCT_LAB7_PROFILE','CSP_UPLOAD_DIAGNOSTICS','CSP_PANIC_DIAGNOSTICS']:
    cases.append(('mixed-'+flag,rc+['%s=1'%flag],False))
for name,defines,valid in cases:
    code='#include "ble_feature.h"\n#include "nimble_stop_repair.h"\n'
    p=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude',
        *['-D'+d for d in defines],'-'],cwd=ROOT,input=code,capture_output=True,text=True)
    assert (p.returncode==0)==valid,(name,p.stderr)
    if valid:assert not p.stderr,(name,p.stderr)
    print('PASS',name)
print('PASS %d actual-header admission cases'%len(cases))
combined=rc+['CSP_PRODUCT_RC2_PROFILE=1','CSP_PRODUCT_SPEED_TRIAL=1','CSP_UPLOAD_THROUGHPUT_TRIAL=1',
             'CSP_PRODUCT_DASHBOARD_TRIAL=1','CSP_ENABLE_LOCAL_DASHBOARD=1','CSP_BACKUP_WIFI_AWAKE=1']
combined_cases=[('combined-rc2',combined,True),('ambiguous-combined',combined+['CSP_PRODUCT_RC2_PROFILE=0'],False)]
for flag in ['CSP_PRODUCT_SPEED_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL','CSP_PRODUCT_DASHBOARD_TRIAL','CSP_ENABLE_LOCAL_DASHBOARD','CSP_BACKUP_WIFI_AWAKE']:
    combined_cases.append(('missing-'+flag,combined+[flag+'=0'],False))
for name,defines,valid in combined_cases:
    code='#include "ble_feature.h"\n#include "nimble_stop_repair.h"\n#include "upload_chunk_feature.h"\n#include "local_dashboard.h"\n#include "backup_wifi_power.h"\n'
    p=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude',*['-D'+d for d in defines],'-'],cwd=ROOT,input=code,capture_output=True,text=True)
    assert (p.returncode==0)==valid,(name,p.stderr)
    if valid:assert not p.stderr,(name,p.stderr)
    print('PASS',name)

rc3=rc+['CSP_PRODUCT_RC3_PROFILE=1','CSP_PRODUCT_SPEED_TRIAL=1','CSP_UPLOAD_THROUGHPUT_TRIAL=1',
 'CSP_PRODUCT_DASHBOARD_TRIAL=1','CSP_ENABLE_LOCAL_DASHBOARD=1','CSP_BACKUP_WIFI_AWAKE=1',
 'CSP_UPLOAD_TX_BYTES=1460','CSP_ENABLE_OWNER_CLOUD_SETUP=1','CSP_ENABLE_OWNER_LOGIN=1',
 'CSP_ENABLE_SCAN_IDLE_GATE=1','CSP_ENABLE_LCD_V2=1','CSP_DASHBOARD_PASSIVE_CLOSE=1']
rc3_cases=[('combined-rc3',rc3,True),('rc3-and-rc2',rc3+['CSP_PRODUCT_RC2_PROFILE=1'],False),
 ('rc3-and-rc4',rc3+['CSP_PRODUCT_RC4_PROFILE=1'],False),
 ('bad-rc3',rc3+['CSP_PRODUCT_RC3_PROFILE=2'],False)]
for flag in ['CSP_PRODUCT_SPEED_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL','CSP_PRODUCT_DASHBOARD_TRIAL',
 'CSP_ENABLE_LOCAL_DASHBOARD','CSP_BACKUP_WIFI_AWAKE','CSP_ENABLE_OWNER_CLOUD_SETUP',
 'CSP_ENABLE_OWNER_LOGIN','CSP_ENABLE_SCAN_IDLE_GATE','CSP_ENABLE_LCD_V2','CSP_DASHBOARD_PASSIVE_CLOSE']:
 rc3_cases.append(('rc3-missing-'+flag,rc3+[flag+'=0'],False))
for name,defines,valid in rc3_cases:
 code='#include "ble_feature.h"\n#include "nimble_stop_repair.h"\n#include "upload_chunk_feature.h"\n#include "local_dashboard.h"\n#include "backup_wifi_power.h"\n#include "owner_cloud_feature.h"\n#include "owner_login_feature.h"\n'
 p=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude',*['-D'+d for d in defines],'-'],cwd=ROOT,input=code,capture_output=True,text=True)
 assert (p.returncode==0)==valid,(name,p.stderr)
 if valid:assert not p.stderr,(name,p.stderr)
 print('PASS',name)


rc4=[flag.replace('CSP_PRODUCT_RC3_PROFILE=1','CSP_PRODUCT_RC4_PROFILE=1') for flag in rc3]
rc4_cases=[('combined-rc4',rc4,True),('rc4-and-rc3',rc4+['CSP_PRODUCT_RC3_PROFILE=1'],False),
 ('rc4-and-rc2',rc4+['CSP_PRODUCT_RC2_PROFILE=1'],False),
 ('rc4-and-speed2',rc4+['CSP_PRODUCT_SPEED2_PROFILE=1'],False),
 ('bad-rc4',rc4+['CSP_PRODUCT_RC4_PROFILE=2'],False),
 ('rc4-wrong-chunk',rc4+['CSP_UPLOAD_TX_BYTES=4096'],False)]
for flag in ['CSP_PRODUCT_SPEED_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL','CSP_PRODUCT_DASHBOARD_TRIAL',
 'CSP_ENABLE_LOCAL_DASHBOARD','CSP_BACKUP_WIFI_AWAKE','CSP_ENABLE_OWNER_CLOUD_SETUP',
 'CSP_ENABLE_OWNER_LOGIN','CSP_ENABLE_SCAN_IDLE_GATE','CSP_ENABLE_LCD_V2','CSP_DASHBOARD_PASSIVE_CLOSE']:
 rc4_cases.append(('rc4-missing-'+flag,rc4+[flag+'=0'],False))
for name,defines,valid in rc4_cases:
 code='#include "ble_feature.h"\n#include "nimble_stop_repair.h"\n#include "upload_chunk_feature.h"\n#include "local_dashboard.h"\n#include "backup_wifi_power.h"\n#include "owner_cloud_feature.h"\n#include "owner_login_feature.h"\n'
 p=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude',*['-D'+d for d in defines],'-'],cwd=ROOT,input=code,capture_output=True,text=True)
 assert (p.returncode==0)==valid,(name,p.stderr)
 if valid:assert not p.stderr,(name,p.stderr)
 print('PASS',name)

rc5=[flag.replace('CSP_PRODUCT_RC4_PROFILE=1','CSP_PRODUCT_RC5_PROFILE=1') for flag in rc4]
rc5_cases=[('combined-rc5',rc5,True),('rc5-and-rc4',rc5+['CSP_PRODUCT_RC4_PROFILE=1'],False),
 ('rc5-and-rc3',rc5+['CSP_PRODUCT_RC3_PROFILE=1'],False),
 ('rc5-and-rc2',rc5+['CSP_PRODUCT_RC2_PROFILE=1'],False),
 ('rc5-and-speed2',rc5+['CSP_PRODUCT_SPEED2_PROFILE=1'],False),
 ('bad-rc5',rc5+['CSP_PRODUCT_RC5_PROFILE=2'],False),
 ('rc5-wrong-chunk',rc5+['CSP_UPLOAD_TX_BYTES=4096'],False),
 ('rc4-and-rc5',rc4+['CSP_PRODUCT_RC5_PROFILE=1'],False),
 ('rc3-and-rc5',rc3+['CSP_PRODUCT_RC5_PROFILE=1'],False)]
for flag in ['CSP_PRODUCT_SPEED_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL','CSP_PRODUCT_DASHBOARD_TRIAL',
 'CSP_ENABLE_LOCAL_DASHBOARD','CSP_BACKUP_WIFI_AWAKE','CSP_ENABLE_OWNER_CLOUD_SETUP',
 'CSP_ENABLE_OWNER_LOGIN','CSP_ENABLE_SCAN_IDLE_GATE','CSP_ENABLE_LCD_V2','CSP_DASHBOARD_PASSIVE_CLOSE']:
 rc5_cases.append(('rc5-missing-'+flag,rc5+[flag+'=0'],False))
for name,defines,valid in rc5_cases:
 code='#include "ble_feature.h"\n#include "nimble_stop_repair.h"\n#include "upload_chunk_feature.h"\n#include "local_dashboard.h"\n#include "backup_wifi_power.h"\n#include "owner_cloud_feature.h"\n#include "owner_login_feature.h"\n'
 p=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude',*['-D'+d for d in defines],'-'],cwd=ROOT,input=code,capture_output=True,text=True)
 assert (p.returncode==0)==valid,(name,p.stderr)
 if valid:assert not p.stderr,(name,p.stderr)
 print('PASS',name)

# A separate development identity retains every RC5 feature without selecting
# the historical RC5 image or permitting mixed profile/version markers.
setup=[flag.replace('CSP_PRODUCT_RC5_PROFILE=1','CSP_PRODUCT_SETUP_DEV_PROFILE=1') for flag in rc5]
setup_cases=[('setup-development',setup,True),
 ('bad-setup-development',setup+['CSP_PRODUCT_SETUP_DEV_PROFILE=2'],False),
 ('setup-wrong-chunk',setup+['CSP_UPLOAD_TX_BYTES=4096'],False),
 ('rc5-and-setup',rc5+['CSP_PRODUCT_SETUP_DEV_PROFILE=1'],False)]
for flag in ['CSP_PRODUCT_RC5_PROFILE','CSP_PRODUCT_RC4_PROFILE','CSP_PRODUCT_RC3_PROFILE','CSP_PRODUCT_RC2_PROFILE','CSP_PRODUCT_SPEED2_PROFILE']:
 setup_cases.append(('setup-and-'+flag,setup+[flag+'=1'],False))
for flag in ['CSP_PRODUCT_SPEED_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL','CSP_PRODUCT_DASHBOARD_TRIAL',
 'CSP_ENABLE_LOCAL_DASHBOARD','CSP_BACKUP_WIFI_AWAKE','CSP_ENABLE_OWNER_CLOUD_SETUP',
 'CSP_ENABLE_OWNER_LOGIN','CSP_ENABLE_SCAN_IDLE_GATE','CSP_ENABLE_LCD_V2','CSP_DASHBOARD_PASSIVE_CLOSE']:
 setup_cases.append(('setup-missing-'+flag,setup+[flag+'=0'],False))
for name,defines,valid in setup_cases:
 code='#include "ble_feature.h"\n#include "nimble_stop_repair.h"\n#include "upload_chunk_feature.h"\n#include "local_dashboard.h"\n#include "backup_wifi_power.h"\n#include "owner_cloud_feature.h"\n#include "owner_login_feature.h"\n'
 p=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude',*['-D'+d for d in defines],'-'],cwd=ROOT,input=code,capture_output=True,text=True)
 assert (p.returncode==0)==valid,(name,p.stderr)
 if valid:assert not p.stderr,(name,p.stderr)
 print('PASS',name)
