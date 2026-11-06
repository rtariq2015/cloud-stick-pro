"""Strict source-only model tests; no device, account or network access."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/remote-health-model'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    names=['include/remote_health_model.h','src/remote_health_model.cpp',
           'test/remote_health_model/model.test.cpp','test/remote_health_model/run.py']
    hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}
    records=[]
    def run(name,args,expected=0):
        p=subprocess.run(args,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
        output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
        diagnostic=bool(re.search(r'runtime error:|AddressSanitizer:|LeakSanitizer:|UndefinedBehaviorSanitizer:',output))
        passed=p.returncode==expected and not diagnostic and (expected!=0 or not p.stderr)
        records.append(dict(name=name,command=args,exit=p.returncode,sanitizer_diagnostic=diagnostic,passed=passed))
        (OUT/'results.json').write_text(json.dumps(dict(passed=all(x['passed'] for x in records),desktop_only=True,
            device_access=False,network_access=False,source_sha256=hashes,records=records),indent=2)+'\n')
        print(('PASS ' if passed else 'FAIL ')+name,flush=True)
        if not passed:print(output[-4000:],file=sys.stderr);raise SystemExit(1)
        return p.stdout
    common=[str(CLANG),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Iinclude']
    strict=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    binary=OUT/'model-tests'
    run('strict-build',[*common,*strict,'-DCSP_ENABLE_REMOTE_HEALTH=1','src/remote_health_model.cpp',
        'test/remote_health_model/model.test.cpp','-o',str(binary)])
    run('strict-tests',[str(binary)])
    for mode,flags in [('default',[]),('disabled',['-DCSP_ENABLE_REMOTE_HEALTH=0']),('enabled',['-DCSP_ENABLE_REMOTE_HEALTH=1'])]:
        obj=OUT/(mode+'.o')
        run(mode+'-object',[*common,*flags,'-fstack-usage','-c','src/remote_health_model.cpp','-o',str(obj)])
        symbols=run(mode+'-symbols',['nm','-C',str(obj)])
        if mode!='enabled':
            assert 'pro::health' not in symbols and b'Last reported snapshot' not in obj.read_bytes()
            assert not run(mode+'-undefined',['nm','-u',str(obj)]).strip()
    run('object-sizes',['size',str(OUT/'enabled.o'),str(OUT/'default.o'),str(OUT/'disabled.o')])
    assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in hashes.items())
    print('PASS source hashes unchanged; default-off objects have no symbols/strings/undefined references',flush=True)

if __name__=='__main__':main()
