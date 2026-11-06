#!/usr/bin/env python3
"""Exact pinned HTTP body-loop + actual adapter under strict ASan/UBSan."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/dav-body-loop';OUT.mkdir(parents=True,exist_ok=True)
CORE=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages/framework-arduinoespressif32')
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def extract(path,signature,name,pinned):
 assert sha(path)==pinned,'Pinned SDK source changed'
 text=path.read_text();start=text.index(signature);pos=text.index('{',start)+1;depth=1
 while depth:depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
 body=text[start:pos]+'\n';(OUT/name).write_text(body)
 return dict(source=str(path),source_sha256=pinned,function_sha256=hashlib.sha256(body.encode()).hexdigest(),signature=signature)
def main():
 actual=[]
 for signature,name in [('int HTTPClient::writeToStreamDataBlock(', 'actual_http_body.inc'),('int HTTPClient::writeToStream(Stream *stream)', 'actual_http_write.inc')]:
  actual.append(extract(CORE/'libraries/HTTPClient/src/HTTPClient.cpp',signature,name,'800cce94ff11c2acc53ed246d43a84d91e4997d144d460bf831cfe24deb9df01'))
 actual.append(extract(CORE/'libraries/Network/src/NetworkClient.cpp','size_t NetworkClient::readBytes(char *buffer, size_t length)','actual_client_read.inc','58c662b52a6fddce366a1b592b5720b33a92d81f2881955f4e81ad2f0f933b15'))
 for signature,name in [('int Stream::timedRead()', 'actual_stream_timed.inc'),('String Stream::readStringUntil(char terminator)', 'actual_stream_line.inc')]:
  actual.append(extract(CORE/'cores/esp32/Stream.cpp',signature,name,'c132fcbe9510316ae366795fd106118111aaa41c8f03ad5f8f9cc4dcbcafd65d'))
 paths=['src/webdav_client.cpp','include/http_response_guard.h',*[str(p.relative_to(ROOT)) for p in (ROOT/'test/dav_body_loop').rglob('*') if p.is_file()]]
 before={p:sha(ROOT/p) for p in paths};records=[]
 commands=[('build',[CLANG,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-Itest/dav_body_loop/stubs','-Itest/backup_integration/dav_stubs','-Iinclude','-Ilib/tinyxml2/src','-I'+str(OUT),'test/dav_body_loop/body_loop.test.cpp','src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp','-lcrypto','-Wl,--wrap=fopen','-o',OUT/'body-loop-tests']),('cases',[OUT/'body-loop-tests'])]
 for name,command in commands:
  r=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180);output=r.stdout+r.stderr;log=OUT/(name+'.log');log.write_text(output)
  diag=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output));secret=any(s in output for s in ['synthetic-user','synthetic-secret','synthetic-ca'])
  passed=r.returncode==0 and not r.stderr and not diag and not secret
  records.append(dict(name=name,command=list(map(str,command)),exit=r.returncode,stderr_empty=not r.stderr,sanitizer_diagnostic=diag,secrets_absent=not secret,passed=passed,log_sha256=sha(log)))
  (OUT/'results.json').write_text(json.dumps(dict(passed=all(x['passed'] for x in records),records=records),indent=2)+'\n');print(name,'PASS' if passed else 'FAIL',flush=True)
  if not passed:raise RuntimeError(output)
 after={p:sha(ROOT/p) for p in paths};assert before==after
 (OUT/'results.json').write_text(json.dumps(dict(passed=True,source_stable=True,source_sha256=after,pinned_functions=actual,strict_asan_ubsan=True,leak_detection=True,device_contact=False,cloud_contact=False,simulated=['TLS','TCP peer bytes','clock','local fixture','receipt storage'],records=records),indent=2)+'\n')
if __name__=='__main__':main()
