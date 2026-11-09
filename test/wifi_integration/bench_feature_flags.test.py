"""Compile-only admission matrix for the normal, isolated and bench targets."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
wifi=['CSP_ENABLE_BLE_MANAGEMENT=1','CSP_ENABLE_WIFI_SETUP=1']
bench=wifi+['CSP_USB_BENCH_AUTOMATION=1']
cases=[('default',[],True,''),('normal-wifi',wifi,True,''),('bench',bench,True,''),
 ('wifi-attempt-trace',bench+['CSP_ENABLE_WIFI_ATTEMPT_TRACE=1'],True,''),
 ('wifi-standard-scan',bench+['CSP_ENABLE_WIFI_STANDARD_SCAN=1'],True,''),
 ('wifi-attempt-trace-without-bench',wifi+['CSP_ENABLE_WIFI_ATTEMPT_TRACE=1'],False,'require the explicit authenticated USB bench'),
 ('wifi-standard-scan-without-bench',wifi+['CSP_ENABLE_WIFI_STANDARD_SCAN=1'],False,'require the explicit authenticated USB bench'),
 ('invalid-wifi-attempt-trace',bench+['CSP_ENABLE_WIFI_ATTEMPT_TRACE=2'],False,'must be 0 or 1'),
 ('invalid-wifi-standard-scan',bench+['CSP_ENABLE_WIFI_STANDARD_SCAN=2'],False,'must be 0 or 1'),
 ('extended-settlement',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_BLE_RELEASE_SETTLEMENT=1','CSP_ENABLE_BLE_EXTENDED_SETTLEMENT=1'],True,''),
 ('extended-without-release',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_BLE_EXTENDED_SETTLEMENT=1'],False,'requires the combined verified-release'),
 ('extended-without-backup',bench+['CSP_ENABLE_BLE_RELEASE_SETTLEMENT=1','CSP_ENABLE_BLE_EXTENDED_SETTLEMENT=1'],False,'requires the combined verified-release'),
 ('invalid-extended-settlement',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_BLE_RELEASE_SETTLEMENT=1','CSP_ENABLE_BLE_EXTENDED_SETTLEMENT=2'],False,'must be 0 or 1'),
 ('bench-trace',bench+['CSP_BLE_ALLOC_TRACE=1'],True,''),
 ('bench-radio-release',bench+['CSP_BENCH_RADIO_RELEASE=1'],True,''),
 ('normal-radio-release-denied',wifi+['CSP_BENCH_RADIO_RELEASE=1'],False,'requires the explicit SD-absent USB bench'),
 ('invalid-radio-release-flag',bench+['CSP_BENCH_RADIO_RELEASE=2'],False,'must be 0 or 1'),
 ('backup-link-probe',bench+['CSP_BENCH_RADIO_RELEASE=1','CSP_BACKUP_LINK_PROBE=1'],True,''),
 ('normal-backup-link-probe-denied',wifi+['CSP_BACKUP_LINK_PROBE=1'],False,'requires the explicit SD-absent radio bench'),
 ('invalid-backup-link-probe',bench+['CSP_BENCH_RADIO_RELEASE=1','CSP_BACKUP_LINK_PROBE=2'],False,'must be 0 or 1'),
 ('combined-backup-wifi',wifi+['CSP_ENABLE_BACKUP_SETUP=1'],True,''),
 ('combined-backup-bench',bench+['CSP_ENABLE_BACKUP_SETUP=1'],True,''),
 ('combined-fixture-only',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_BACKUP_FIXTURE_ONLY=1'],True,''),
 ('combined-without-wifi',['CSP_ENABLE_BACKUP_SETUP=1'],False,'Combined backup setup requires'),
 ('combined-link-probe-denied',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_BACKUP_LINK_PROBE=1'],False,'Combined backup setup requires'),
 ('combined-allocation-trace-denied',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_BLE_ALLOC_TRACE=1'],False,'Combined backup setup requires'),
 ('invalid-combined-flag',wifi+['CSP_ENABLE_BACKUP_SETUP=2'],False,'must be 0 or 1'),
 ('fixture-without-combined',bench+['CSP_BACKUP_FIXTURE_ONLY=1'],False,'requires the explicit combined USB bench'),
 ('fixture-without-bench',wifi+['CSP_ENABLE_BACKUP_SETUP=1','CSP_BACKUP_FIXTURE_ONLY=1'],False,'requires the explicit combined USB bench'),
 ('invalid-fixture-flag',bench+['CSP_ENABLE_BACKUP_SETUP=1','CSP_BACKUP_FIXTURE_ONLY=2'],False,'must be 0 or 1'),
 ('normal-wifi-trace-denied',wifi+['CSP_BLE_ALLOC_TRACE=1'],False,'Allocation tracing requires'),
 ('bench-without-wifi-denied',['CSP_USB_BENCH_AUTOMATION=1'],False,'USB bench automation requires'),
 ('invalid-bench-flag',wifi+['CSP_USB_BENCH_AUTOMATION=2'],False,'must be 0 or 1'),
 ('public-proof-denied',bench+['CSP_BLE_SYNTHETIC_ONLY=1'],False,'Public M1 credentials may never'),
 ('mixed-isolation-denied',bench+['CSP_BLE_ISOLATED_TEST=1'],False,'Wi-Fi setup requires')]
for name,defines,valid,diagnostic in cases:
    r=subprocess.run(['g++','-x','c++','-fsyntax-only','-Iinclude','-include','ble_feature.h',
                      *['-D'+d for d in defines],'-'],cwd=ROOT,input='',capture_output=True,text=True)
    assert (r.returncode==0)==valid,name
    if valid:assert not r.stderr,name
    else:assert diagnostic in r.stderr,name
    print('PASS',name)
