"""Header layout only, using existing native and pinned target compilers.

No PlatformIO, SDK mutation, linked firmware image or hardware operation.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/local-dashboard-layout'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
TARGET=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages/toolchain-xtensa-esp-elf/bin')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
defines=['-DCSP_ENABLE_LOCAL_DASHBOARD=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_PRODUCT_DASHBOARD_TRIAL=1']
sources=['include/local_dashboard.h','test/local_dashboard/layout.test.cpp','test/local_dashboard/layout.test.py']
hashes={path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in sources}
records=[]
for name,compiler,nm in [('native',DEPS/'lib/llvm-18/bin/clang++',Path('/usr/bin/nm')),
                         ('esp32s3',TARGET/'xtensa-esp32s3-elf-g++',TARGET/'xtensa-esp-elf-nm')]:
    version=subprocess.run([str(compiler),'--version'],capture_output=True,text=True,env=ENV,check=True).stdout.splitlines()[0]
    for flag in ('default','0','1'):
        obj=OUT/(name+'-'+flag+'.o')
        command=[str(compiler),'-std=c++17','-Wall','-Wextra','-Werror','-Iinclude',*defines,
                 *([] if flag=='default' else ['-DCSP_DASHBOARD_PASSIVE_CLOSE='+flag]),
                 '-c','test/local_dashboard/layout.test.cpp','-o',str(obj)]
        built=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True)
        assert built.returncode==0 and not built.stderr,built.stdout+built.stderr
        listed=subprocess.run([str(nm),'-S','--radix=d',str(obj)],env=ENV,capture_output=True,text=True,check=True)
        symbols={parts[3]:int(parts[1]) for line in listed.stdout.splitlines() if len(parts:=line.split())==4 and parts[3].startswith('dashboard_')}
        assert set(symbols)=={'dashboard_server_layout','dashboard_caller_buffer_layout','dashboard_passive_flag'}
        assert symbols['dashboard_caller_buffer_layout']==3201
        assert symbols['dashboard_passive_flag']==(2 if flag=='1' else 1)
        records.append(dict(platform=name,flag=flag,compiler=version,compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),
            command=command,symbol_bytes=symbols,object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))
    baseline=next(row for row in records if row['platform']==name and row['flag']=='0')
    default=next(row for row in records if row['platform']==name and row['flag']=='default')
    enabled=next(row for row in records if row['platform']==name and row['flag']=='1')
    assert default['symbol_bytes']==baseline['symbol_bytes']
    delta=enabled['symbol_bytes']['dashboard_server_layout']-baseline['symbol_bytes']['dashboard_server_layout']
    print(name,'PASS default=0, caller buffer3201bytes, Server',baseline['symbol_bytes']['dashboard_server_layout'],'->',enabled['symbol_bytes']['dashboard_server_layout'],'delta',delta)
    invalid=subprocess.run([str(compiler),'-std=c++17','-Iinclude',*defines,'-DCSP_DASHBOARD_PASSIVE_CLOSE=2','-fsyntax-only','test/local_dashboard/layout.test.cpp'],cwd=ROOT,env=ENV,capture_output=True,text=True)
    assert invalid.returncode!=0 and 'CSP_DASHBOARD_PASSIVE_CLOSE must be 0 or 1' in invalid.stderr
    (OUT/(name+'-invalid-flag.log')).write_text(invalid.stdout+invalid.stderr)
assert all(hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==value for path,value in hashes.items())
(OUT/'results.json').write_text(json.dumps(dict(passed=True,source_only=True,linked_firmware=False,
    device_contact=False,source_sha256=hashes,invalid_flag_rejected=True,records=records),indent=2)+'\n')
