#!/usr/bin/env python3
"""Strict actual-adapter and pinned-loop diagnostics tests; no network/device."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/upload-diagnostics';OUT.mkdir(parents=True,exist_ok=True)
CORE=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages/framework-arduinoespressif32')
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
ENABLED=['-DCSP_UPLOAD_DIAGNOSTICS=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_USB_BENCH_AUTOMATION=1']
RECORD=[]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(name,args,expected=None):
    r=subprocess.run(list(map(str,args)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=r.stdout+r.stderr;log=OUT/(name+'.log');log.write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    passed=(r.returncode!=0 and expected in r.stderr) if expected else r.returncode==0 and not r.stderr and not diagnostic
    secret_free=all(value not in output for value in ['SYNTHETIC_SECRET_NEVER_LOG','fixture-user','fixture-ca'])
    passed=passed and secret_free and not diagnostic
    RECORD.append(dict(name=name,command=list(map(str,args)),exit=r.returncode,expected_rejection=bool(expected),
                       sanitizer_diagnostic=diagnostic,stderr_empty=not r.stderr,passed=passed,
                       secrets_absent=secret_free,log_sha256=sha(log)))
    (OUT/'results.json').write_text(json.dumps(dict(passed=all(row['passed'] for row in RECORD),records=RECORD),indent=2)+'\n')
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:raise RuntimeError(output)
    return r.stdout
def extract(source,signature,name,pinned):
    assert sha(source)==pinned,'Pinned SDK source changed'
    text=source.read_text();start=text.index(signature);brace=text.index('{',start);at=brace+1;depth=1
    while depth:depth+=(text[at]=='{')-(text[at]=='}');at+=1
    value=text[start:at]+'\n';(OUT/name).write_text(value)
    return dict(source=str(source),source_sha256=pinned,function_sha256=hashlib.sha256(value.encode()).hexdigest())
def main():
    sources=['src/webdav_client.cpp','src/upload_diagnostics.cpp','include/upload_diagnostics.h',
             'test/upload_diagnostics/transport.test.cpp','test/upload_diagnostics/run.py',
             'test/upload_diagnostics/fake_tls.cpp',
             'test/upload_diagnostics/fake_net.cpp',
             *[str(p.relative_to(ROOT)) for p in (ROOT/'test/upload_diagnostics/stubs').rglob('*') if p.is_file()]]
    before={p:sha(ROOT/p) for p in sources}
    actual=[extract(CORE/'libraries/NetworkClientSecure/src/ssl_client.cpp','int send_ssl_data(',
                    'actual_send_ssl_data.inc','85755cb020d9671a4bf2e254c2be33e5506c93db65a921f7554f59ab0a4afc78'),
            extract(CORE/'libraries/NetworkClientSecure/src/NetworkClientSecure.cpp','size_t NetworkClientSecure::write(const uint8_t *buf, size_t size)',
                    'actual_secure_write.inc','2c614113580fc9458df53aeae3e5a024ba78823cb768619874a6fcaba92b30d0'),
            extract(CORE/'libraries/HTTPClient/src/HTTPClient.cpp','int HTTPClient::sendRequest(const char *type, Stream *stream, size_t size)',
                    'actual_http_stream.inc','800cce94ff11c2acc53ed246d43a84d91e4997d144d460bf831cfe24deb9df01')]
    run('actual-transport-build',[CLANG,*FLAGS,*ENABLED,'-Itest/upload_diagnostics/stubs','-Itest/backup_integration/dav_stubs',
        '-Iinclude','-Ilib/tinyxml2/src','-I'+str(OUT),'src/webdav_client.cpp','src/upload_diagnostics.cpp',
        'src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp','test/upload_diagnostics/transport.test.cpp','test/upload_diagnostics/fake_tls.cpp','test/upload_diagnostics/fake_net.cpp','-lcrypto',
        '-Wl,--wrap=fopen','-Wl,--wrap=fread','-Wl,--wrap=mbedtls_ssl_write','-Wl,--wrap=mbedtls_ssl_read','-Wl,--wrap=mbedtls_net_send',
        '-o',OUT/'actual-transport'])
    output=run('actual-transport',[OUT/'actual-transport'])
    cases,size=map(int,re.search(r'(\d+) actual adapter/HTTP/secure/TLS diagnostic case groups passed; snapshot_bytes=(\d+)',output).groups())
    assert size<=256
    for mode,defines in [('default',[]),('off',['-DCSP_UPLOAD_DIAGNOSTICS=0'])]:
        obj=OUT/(mode+'.o')
        run(mode+'-object',[CLANG,'-std=c++17','-Wall','-Wextra','-Werror',*defines,'-Iinclude','-c','src/upload_diagnostics.cpp','-o',obj])
        symbols=run(mode+'-symbols',['nm','--defined-only',obj]);assert not symbols.strip()
    run('reject-invalid-flag',[CLANG,'-std=c++17','-DCSP_UPLOAD_DIAGNOSTICS=2','-Iinclude','-c','src/upload_diagnostics.cpp','-o',OUT/'invalid.o'],expected='CSP_UPLOAD_DIAGNOSTICS must be zero or one')
    run('reject-nonbench',[CLANG,'-std=c++17','-DCSP_UPLOAD_DIAGNOSTICS=1','-Iinclude','-c','src/upload_diagnostics.cpp','-o',OUT/'nonbench.o'],expected='Upload diagnostics require the explicit combined USB bench profile')
    after={p:sha(ROOT/p) for p in sources};assert before==after
    (OUT/'results.json').write_text(json.dumps(dict(passed=True,desktop_only=True,device_contact=False,cloud_contact=False,
        actual_production_code=True,strict_asan_ubsan=True,leak_detection=True,case_groups=cases,snapshot_bytes=size,
        source_sha256=after,source_stable=True,pinned_extracted_functions=actual,records=RECORD),indent=2)+'\n')
if __name__=='__main__':main()
