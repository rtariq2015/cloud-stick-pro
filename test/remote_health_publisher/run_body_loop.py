#!/usr/bin/env python3
"""Health publisher against exact pinned HTTP/Stream/readBytes bodies."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/remote-health-body-loop';OUT.mkdir(parents=True,exist_ok=True)
CORE=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages/framework-arduinoespressif32')
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def extract(path,signature,name,pinned):
    assert sha(path)==pinned,'Pinned SDK source changed'
    text=path.read_text();start=text.index(signature);pos=text.index('{',start)+1;depth=1
    while depth:depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    body=text[start:pos]+'\n';(OUT/name).write_text(body)
    return dict(source=str(path),source_sha256=pinned,function_sha256=hashlib.sha256(body.encode()).hexdigest(),signature=signature)
def main():
    pinned=[]
    for sig,name in [('int HTTPClient::writeToStreamDataBlock(','actual_http_body.inc'),('int HTTPClient::writeToStream(Stream *stream)','actual_http_write.inc')]:
        pinned.append(extract(CORE/'libraries/HTTPClient/src/HTTPClient.cpp',sig,name,'800cce94ff11c2acc53ed246d43a84d91e4997d144d460bf831cfe24deb9df01'))
    pinned.append(extract(CORE/'libraries/Network/src/NetworkClient.cpp','size_t NetworkClient::readBytes(char *buffer, size_t length)','actual_client_read.inc','58c662b52a6fddce366a1b592b5720b33a92d81f2881955f4e81ad2f0f933b15'))
    for sig,name in [('int Stream::timedRead()','actual_stream_timed.inc'),('String Stream::readStringUntil(char terminator)','actual_stream_line.inc')]:
        pinned.append(extract(CORE/'cores/esp32/Stream.cpp',sig,name,'c132fcbe9510316ae366795fd106118111aaa41c8f03ad5f8f9cc4dcbcafd65d'))
    paths=['src/remote_health_publisher.cpp','include/http_response_guard.h','src/webdav_client.cpp',
           'include/remote_health_publisher.h','include/webdav_client.h','include/remote_health_model.h',
           'src/remote_health_model.cpp','src/pro_core.cpp','include/pro_core.h',
           'lib/tinyxml2/src/tinyxml2.cpp','lib/tinyxml2/src/tinyxml2.h']
    paths += [str(p.relative_to(ROOT)) for p in (ROOT/'test/remote_health_publisher').rglob('*') if p.is_file()]
    paths += [str(p.relative_to(ROOT)) for p in (ROOT/'test/backup_integration/dav_stubs').rglob('*') if p.is_file()]
    before={p:sha(ROOT/p) for p in paths};records=[]
    commands=[('build',[DEPS/'lib/llvm-18/bin/clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                       '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_REMOTE_HEALTH=1',
                       '-Itest/remote_health_publisher/body_stubs','-Itest/backup_integration/dav_stubs','-Iinclude','-Ilib/tinyxml2/src','-I'+str(OUT),
                       'test/remote_health_publisher/body_loop.test.cpp','src/remote_health_model.cpp','src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp',
                       '-lcrypto','-Wl,--wrap=fopen','-o',OUT/'health-body-tests']),('cases',[OUT/'health-body-tests'])]
    for name,cmd in commands:
        p=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
        text=p.stdout+p.stderr;log=OUT/(name+'.log');log.write_text(text)
        diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
        passed=p.returncode==0 and not p.stderr and not diagnostic
        records.append(dict(name=name,command=list(map(str,cmd)),exit=p.returncode,stderr_empty=not p.stderr,sanitizer_diagnostic=diagnostic,passed=passed,log_sha256=sha(log)))
        (OUT/'results.json').write_text(json.dumps(dict(passed=all(x['passed'] for x in records),records=records),indent=2)+'\n')
        print(name,'PASS' if passed else 'FAIL',flush=True)
        if not passed:raise RuntimeError(text)
    after={p:sha(ROOT/p) for p in paths};assert before==after,'Linked source or fixture changed during gate'
    (OUT/'results.json').write_text(json.dumps(dict(passed=True,source_stable=True,source_sha256=after,pinned_functions=pinned,
      strict_asan_ubsan=True,leak_detection=True,device_contact=False,cloud_contact=False,
      simulated=['TCP/TLS bytes','clock','Wi-Fi','DAV responses'],records=records),indent=2)+'\n')
if __name__=='__main__':main()
