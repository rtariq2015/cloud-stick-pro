"""Strict ASan/UBSan tests of actual pure lifecycle headers; no SDK/device."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/setup-duration'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=Path(shutil.which('clang++') or DEPS/'lib/llvm-18/bin/clang++')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all',
       '-fno-omit-frame-pointer','-Iinclude']
SOURCES=['include/pro_setup_session.h','include/pro_ble_lifecycle.h',
         'test/setup_duration/lifecycle.test.cpp','test/setup_duration/lifecycle_tests.py',
         'test/ble_lifecycle.test.cpp','test/ble_lifecycle_clock.test.cpp']


def hashes():
    return {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCES}


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    before=hashes()
    records=[]
    def run(name,command):
        value=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=90)
        log=OUT/(name+'.log')
        log.write_text(value.stdout+value.stderr)
        diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',value.stdout+value.stderr))
        passed=value.returncode==0 and not value.stderr and not diagnostic
        records.append({'name':name,'command':list(map(str,command)),'exit':value.returncode,
                        'stderr_empty':not value.stderr,'sanitizer_diagnostic':diagnostic,'passed':passed,
                        'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
        print(name,'PASS' if passed else 'FAIL',flush=True)
        if not passed:
            raise RuntimeError(name+' failed; inspect '+str(log))
        return value.stdout
    version=subprocess.run([str(CLANG),'--version'],cwd=ROOT,env=ENV,capture_output=True,text=True,check=True).stdout.splitlines()[0]
    result={'scope':'Pure source lifecycle only; no SDK, device, serial, network or private inputs',
            'compiler':str(CLANG),'compiler_version':version,'device_contact':False,'sdk_build':False,'records':records}
    try:
        for name,defines in [('default',[]),('rc5',['-DCSP_PRODUCT_RC5_PROFILE=1']),
                             ('guided',['-DCSP_PRODUCT_SETUP_DEV_PROFILE=1'])]:
            binary=OUT/('lifecycle-'+name)
            run('lifecycle-'+name+'-build',[CLANG,*FLAGS,*defines,'test/setup_duration/lifecycle.test.cpp','-o',binary])
            run('lifecycle-'+name,[binary])
        for name,source in [('existing-default','test/ble_lifecycle.test.cpp'),
                            ('existing-clock','test/ble_lifecycle_clock.test.cpp')]:
            binary=OUT/('lifecycle-'+name)
            run('lifecycle-'+name+'-build',[CLANG,*FLAGS,source,'-o',binary])
            run('lifecycle-'+name,[binary])
        after=hashes()
        assert before==after,'Lifecycle source changed during tests'
        result.update(passed=True,source_stable=True,source_sha256=after,profiles_tested=['default','rc5','guided'],
                      new_lifecycle_groups_per_profile=6,repeated_sessions_per_profile=100)
    except BaseException:
        result.update(passed=False,source_stable=before==hashes())
        (OUT/'lifecycle-results.json').write_text(json.dumps(result,indent=2)+'\n')
        raise
    (OUT/'lifecycle-results.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':
    main()
