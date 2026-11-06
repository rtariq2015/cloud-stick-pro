#!/usr/bin/env python3
"""Record source-only product regressions; no PIO, device, cloud or site access."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys,time

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/product-lab4-build/source-gates'
PY='/config/.unmanic/research/cloud-stick-tools/bin/python3'
GROUPS=[
 ('production-core',['bash','test/run.sh']),
 ('ble-policy',['bash','test/run_ble_native.sh']),
 ('worker',['bash','test/backup_integration/run_worker_tests.sh']),
 ('memory-observer',[PY,'test/backup_integration/memory_observer.test.py']),
 ('wifi-buffer-policy',[PY,'test/backup_integration/wifi_buffer.test.py']),
 ('backup-wording',[PY,'test/backup_integration/backup_wording_tests.py']),
 ('owned-route',[PY,'test/owned_guest_route/run_tests.py']),
 ('owned-route-liveness',[PY,'test/owned_guest_route/ap_liveness_tests.py']),
 ('route-startup-restart',[PY,'test/owned_guest_route/configuration_restart_tests.py']),
 ('route-diagnostics',[PY,'test/route_diagnostics/run.py']),
 ('dav',[PY,'test/backup_integration/dav_cancel.test.py']),
 ('config',[PY,'test/backup_integration/config_loader.test.py']),
 ('runtime',[PY,'tools/test_backup_runtime.py']),
 ('phone',[PY,'test/backup_integration/phone_backup_tests.py']),
 ('features',[PY,'test/wifi_integration/bench_feature_flags.test.py']),
 ('wifi-runtime-sdk',[PY,'tools/test_wifi_integration.py']),
 ('wifi-sdk-protocol',['node','--test','test/wifi_integration/connection-sdk.test.mjs']),
 ('sdk-lifecycle',[PY,'test/milestone1/test_sdk_lifecycle.py']),
 ('native-bridge',[PY,'test/wifi_integration/native_bluez.test.py']),
 ('public-wifi',[PY,'test/public_wifi_assistance/run_tests.py']),
 ('event-log',[PY,'test/event_log/run.py']),
 ('event-export',[PY,'test/event_log/run_export.py']),
 ('event-phone',[PY,'test/event_log_phone/run.py']),
 ('file-history',[PY,'test/file_history/run.py']),
 ('history-phone',[PY,'test/file_history/run_phone.py']),
 ('health-model',[PY,'test/remote_health_model/run.py']),
 ('health-publisher',[PY,'test/remote_health_publisher/run.py']),
 ('health-worker',[PY,'test/remote_health_worker/run.py']),
 ('combined-worker',[PY,'test/combined_product_worker/run.py']),
 ('dav-body-loop',[PY,'test/dav_body_loop/run.py']),
 ('health-body-loop',[PY,'test/remote_health_publisher/run_body_loop.py']),
 ('build-guard',[PY,'tools/build_public_wifi_memory_profile.py','--self-test']),
 ('nimble-stop',[PY,'test/nimble_stop/run.py']),
 ('product-profile',[PY,'test/product_rc/profile.test.py']),
 ('local-dashboard',[PY,'test/local_dashboard/run.py']),
 ('dashboard-runtime',[PY,'test/local_dashboard/runtime.test.py']),
 ('dashboard-object-layout',[PY,'test/local_dashboard/layout.test.py']),
 ('http-upload-chunk',[PY,'test/http_upload_chunk/run.py']),
 ('backup-wifi-power',[PY,'test/backup_wifi_power/run.py']),
 ('cloud-model-wire',[PY,'test/cloud_setup/native.py']),
 ('cloud-store',[PY,'test/cloud_setup/store_tests.py']),
 ('cloud-runtime',[PY,'test/cloud_setup/runtime_native.py']),
 ('owner-login-model-wire',[PY,'test/owner_login/native.py']),
 ('owner-login-store',[PY,'test/owner_login/store_tests.py']),
 ('owner-login-sdk',[PY,'test/owner_login/security2_tests.py']),
 ('owner-login-runtime',[PY,'test/owner_login/runtime_native.py']),
 ('owner-login-integrated-runtime',['env','CSP_OWNER_LOGIN_INTEGRATED=1',PY,'test/owner_login/runtime_native.py']),
 ('owner-login-adapter',[PY,'test/owner_login/adapter_tests.py']),
 ('owner-login-phone',['node','--test','test/owner_login/connection.test.mjs','test/owner_login/crypto.test.mjs','test/owner_login/ui.test.mjs']),
 ('owner-login-sdk-interop',['node','--test','test/owner_login/sdk-interop.test.mjs']),
 ('owner-login-package',[PY,'test/owner_login/package.test.py']),
 ('owner-login-browser',['env','PLAYWRIGHT_BROWSERS_PATH=/config/.unmanic/research/cloud-stick-browsers',
    'LD_LIBRARY_PATH=/config/.unmanic/research/cloud-stick-wifi-browser-deps/root/usr/lib/x86_64-linux-gnu',
    PY,'test/owner_login/browser_preview.py']),
 ('wifi-phone-package',[PY,'test/wifi_integration/https_package.test.py']),
 ('cloud-phone',['node','--test','test/cloud_setup/phone.test.mjs']),
 ('scan-idle-gate',[PY,'test/scan_idle_gate/run.py']),
 ('file-incremental-cache',[PY,'test/incremental_cache/run.py']),
 ('lcd-v2',[PY,'test/lcd_v2/run.py',]),
 ('lcd-renderer',[PY,'test/lcd_v2/render.py']),

]

def snapshot():
    names=subprocess.check_output(['git','ls-files','-c','--others','--exclude-standard','--',
        'src','include','lib','phone','test','tools','platformio.ini','partitions.csv','boards','sdk'],cwd=ROOT,text=True).splitlines()
    return {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(names)) if (ROOT/p).is_file()}

def main():
    global OUT
    parser=argparse.ArgumentParser();parser.add_argument('--output')
    args=parser.parse_args()
    if args.output:
        requested=Path(args.output)
        assert not requested.is_absolute() and '..' not in requested.parts
        OUT=ROOT/requested
        assert OUT.resolve().is_relative_to((ROOT/'artifacts').resolve())
    OUT.mkdir(parents=True,exist_ok=True);before=snapshot();records=[]
    env=dict(os.environ,PATH='/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr/lib/llvm-18/bin:'+os.environ['PATH'],
        LD_LIBRARY_PATH='/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr/lib/x86_64-linux-gnu',
        ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
        UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
        CSP_OWNER_LOGIN_SDK_HOST=str(ROOT/'artifacts/wifi-integration/sdk-runtime-host'))
    result={'scope':'Desktop source/SDK-adapter only','hardware':False,'PIO':False,
        'real_cloud':False,'source_sha256':before,'records':records,'passed':False}
    for name,command in GROUPS:
        started=time.monotonic()
        p=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=600)
        stdout=OUT/(name+'.stdout.log');stderr=OUT/(name+'.stderr.log')
        stdout.write_text(p.stdout);stderr.write_text(p.stderr)
        diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',p.stdout+p.stderr))
        skipped=bool(re.search(r'^# skipped [1-9][0-9]*$',p.stdout,re.MULTILINE))
        passed=p.returncode==0 and not diagnostic and not skipped
        records.append({'name':name,'command':command,'exit':p.returncode,'seconds':round(time.monotonic()-started,3),
            'stderr_present':bool(p.stderr),'sanitizer_diagnostic':diagnostic,'skipped_tests':skipped,'passed':passed,
            'stdout_sha256':hashlib.sha256(stdout.read_bytes()).hexdigest(),
            'stderr_sha256':hashlib.sha256(stderr.read_bytes()).hexdigest()})
        print(name,'PASS' if passed else 'FAIL',flush=True)
        result['source_stable']=before==snapshot()
        (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        if not passed or not result['source_stable']:
            print((p.stdout+p.stderr)[-4000:]);return 1
    result['passed']=True
    (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS all source groups; source stable; no sanitizer diagnostics',flush=True)
    return 0

if __name__=='__main__':sys.exit(main())
