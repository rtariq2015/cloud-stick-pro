"""Source-only custom owner-login interoperability; pinned SDK/reference unchanged."""
from pathlib import Path
import hashlib,importlib.util,json,os,re,subprocess,sys,zlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/owner-login-security2';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
BASE=ROOT/'test/milestone1'
SDK=Path('/config/.unmanic/research/cloud-stick-platformio/packages/framework-arduinoespressif32-libs/esp32s3/include/protocomm')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
reference=ROOT/'phone/milestone1/test/reference/srp6a.py'
assert hashlib.sha256(reference.read_bytes()).hexdigest()=='9dc802e1540cce95220479cf1c56a836beeeba0869117fd3904008ac67f5537c'
sys.path.insert(0,str(reference.parent));spec=importlib.util.spec_from_file_location('owner_login_srp_reference',reference);srp=importlib.util.module_from_spec(spec);spec.loader.exec_module(srp)
def record(username,password,seed):
 assert hashlib.sha512((username+':'+password).encode()).digest()[0]
 group=srp.Srp6a(username,password);salt=bytes(seed+i for i in range(16))
 x=srp.calculate_x(hashlib.sha512,int.from_bytes(salt,'big'),username,password)
 verifier=pow(group.g,x,group.N).to_bytes(384,'big');assert verifier[0]
 raw=bytearray(432);raw[:4]=b'COW2';raw[4]=2;raw[6:12]=bytes.fromhex('001122334455');raw[12:28]=salt;raw[28:412]=verifier
 raw[428:432]=zlib.crc32(raw[:428]).to_bytes(4,'little');return bytes(raw)
fixtures=[record('csp-owner','PUBLIC-SYNTHETIC-OLD-OWNER-ONLY',17),record('operator-synthetic','PUBLIC-SYNTHETIC-CUSTOM-LOGIN-42',49)]
(OUT/'synthetic_owner_records.h').write_text('/* Public synthetic fixtures; generated using unchanged Espressif reference. */\n'+ '\n'.join('static const unsigned char '+name+'[432]={'+','.join(str(x) for x in raw)+'};' for name,raw in zip(['OldRecord','NewRecord'],fixtures))+'\n')
files=['include/owner_login_feature.h','include/ble_feature.h','lib/owner_login/owner_login_store.h','lib/owner_login/owner_login_store.cpp',
 'lib/wifi_setup_model/owner_record.h','lib/wifi_setup_model/owner_record.cpp','lib/wifi_setup_model/owner_group.h','lib/wifi_setup_model/wifi_profile_store.h',
 'lib/wifi_setup_model/wifi_setup_model.h','lib/wifi_setup_model/wifi_setup_model.cpp','phone/wifi-setup/security2.mjs','phone/milestone1/test/sdk_host.mjs',
 'phone/milestone1/test/reference/srp6a.py','test/owner_login/security2_host.cpp','test/owner_login/security2.test.mjs','test/owner_login/security2_tests.py',
 'tools/pinned_ble_sdk.py','sdk/pinned_ble/PROVENANCE.json','sdk/pinned_ble/patched/security2.c','test/milestone1/sdk_vendor/PROVENANCE.json']
names=['esp_srp.c','esp_srp_mpi.c','session.pb-c.c','sec0.pb-c.c','sec1.pb-c.c','sec2.pb-c.c','constants.pb-c.c']
files+=['test/milestone1/sdk_vendor/'+name for name in names]+['test/milestone1/host_shims/'+p.name for p in (BASE/'host_shims').iterdir() if p.is_file()]
hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files};records=[]
def publish(done=False):
 stable=all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in hashes.items())
 passed=done and stable and all(x['passed'] for x in records)
 (OUT/'results.json').write_text(json.dumps(dict(passed=passed,complete=done,source_stable=stable,desktop_only=True,device_access=False,network_access=False,real_credentials=False,
  unchanged_reference=True,actual_Store_COW2=True,actual_pinned_Security2=True,source_sha256=hashes,synthetic_record_sha256=[hashlib.sha256(x).hexdigest() for x in fixtures],records=records),indent=2)+'\n');return passed
def run(name,cmd):
 value=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180);text=value.stdout+value.stderr;(OUT/(name+'.log')).write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 secrets_absent=all(secret not in text for secret in ['PUBLIC-SYNTHETIC-OLD-OWNER-ONLY','PUBLIC-SYNTHETIC-CUSTOM-LOGIN-42'])
 okay=value.returncode==0 and not value.stderr and not diagnostic and secrets_absent;records.append(dict(name=name,command=list(map(str,cmd)),exit=value.returncode,passed=okay,sanitizer_diagnostic=diagnostic,synthetic_passwords_absent=secrets_absent));publish();print(name,'PASS' if okay else 'FAIL',flush=True)
 if not okay:print(text[-6000:]);raise SystemExit(1)
publish();run('sdk-provenance',[sys.executable,'tools/pinned_ble_sdk.py'])
provenance=json.loads((BASE/'sdk_vendor/PROVENANCE.json').read_text())
for name,entry in provenance['files'].items():
 raw=(BASE/'sdk_vendor'/name).read_bytes();assert hashlib.sha1(('blob '+str(len(raw))+'\0').encode()+raw).hexdigest()==entry['git_blob_sha']
includes=[BASE/'host_shims',BASE/'sdk_vendor',SDK/'include/security',SDK/'include/crypto/srp6a',SDK/'proto-c',DEPS/'include']
objects=[]
for name in ['security2.c']+names:
 source=ROOT/'sdk/pinned_ble/patched/security2.c' if name=='security2.c' else BASE/'sdk_vendor'/name
 target=OUT/(name+'.o');objects.append(target)
 command=['gcc','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
  '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-include',BASE/'host_shims/mbedtls_compat.h']
 for path in includes:command+=['-I',path]
 run('build-'+name,command+['-c',source,'-o',target])
binary=OUT/'owner-login-security2-host'
command=[DEPS/'lib/llvm-18/bin/clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
 '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCSP_ENABLE_OWNER_LOGIN=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1',
 '-Iinclude','-Ilib/owner_login','-Ilib/wifi_setup_model','-I'+str(OUT)]
for path in includes:command+=['-I',path]
run('build-host',command+['test/owner_login/security2_host.cpp','lib/owner_login/owner_login_store.cpp','lib/wifi_setup_model/owner_record.cpp',
 'lib/wifi_setup_model/wifi_setup_model.cpp',*objects,'-L'+str(DEPS/'lib/x86_64-linux-gnu'),'-lmbedtls','-lmbedx509','-lmbedcrypto','-lprotobuf-c','-o',binary])
ENV['CSP_OWNER_LOGIN_SECURITY2_HOST']=str(binary)
run('actual-store-sdk-interop',['node','test/owner_login/security2.test.mjs'])
if not publish(done=True):raise SystemExit('FAIL: source changed during interop; stable rerun required')
