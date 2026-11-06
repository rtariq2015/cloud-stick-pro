"""Desktop regression and pinned-source verification; never touches hardware.
The radio fixture models the documented IDF DHCP reset, not full target lwIP.
"""
from pathlib import Path
import hashlib,json,os,re,resource,subprocess,urllib.request

resource.setrlimit(resource.RLIMIT_CORE,(0,0))
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/os.environ.get('CSP_WIFI_TEST_OUT','artifacts/wifi-startup-correction')
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
url='https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/components/esp_netif/lwip/esp_netif_lwip.c'
expected_sha='f5c1f6fd080df5416cb8893578648609e3da93cadebddb569173739f3bfb9a34'
cached=OUT/'esp_netif_lwip.c'
if cached.exists():upstream=cached.read_bytes()
else:
 with urllib.request.urlopen(url,timeout=30) as response:upstream=response.read()
assert hashlib.sha256(upstream).hexdigest()==expected_sha,'Pinned SDK source changed'
cached.write_bytes(upstream)
text=upstream.decode()
assert 'esp_netif->dhcpc_status = ESP_NETIF_DHCP_INIT;' in text
assert 'esp_netif->dhcpc_status != ESP_NETIF_DHCP_STOPPED' in text
source=(ROOT/'src/wifi_setup_radio.cpp').read_text()
assert source.count('if(!barrier()||!dhcpStop())return false;')==1
mutant=OUT/'old-order-radio.cpp'
mutant.write_text(source.replace('if(!barrier()||!dhcpStop())return false;',
 'if(!dhcpStop()||!barrier())return false;'))
fixture=OUT/'old-order-test.cpp'
fixture.write_text((ROOT/'test/wifi_integration/radio.test.cpp').read_text().replace(
 '#include "../../src/wifi_setup_radio.cpp"','#include "'+str(mutant)+'"'))
record=json.loads((OUT/'build-record.json').read_text())
command=next(r['command'] for r in record['commands'] if r['name']=='radio-build')
command=[str(fixture) if x=='test/wifi_integration/radio.test.cpp' else x for x in command]
command[command.index('-o')+1]=str(OUT/'old-order-test')
command[1:1]=['-Itest/wifi_integration']
r=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=90)
(OUT/'old-order-build.log').write_text(r.stdout+r.stderr)
assert r.returncode==0 and not r.stderr
r=subprocess.run([str(OUT/'old-order-test')],cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=15)
(OUT/'old-order-reproduction.log').write_text(r.stdout+r.stderr)
assert r.returncode==-6
assert re.fullmatch(r'\d+: wifi_setup_radio_snapshot\(\)\.state==WifiSetupRadioState::Failed\n',r.stderr),r.stderr
result={'passed':True,'corrected_radio_suite_passes':True,'old_order_reproduces_fault_in_fixture':True,
 'old_exit':r.returncode,'expected_assertion':r.stderr.strip(),
 'unexpected_sanitizer_output_accepted':False,'upstream':url,
 'upstream_sha256':hashlib.sha256(upstream).hexdigest(),
 'scope':'Actual radio source with a modeled queued DHCP reset; full SDK lwIP and hardware timing are not executed',
 'physical_startup_root_cause_proven':False}
(OUT/'stop-order-results.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS corrected order; old order fails exact expected assertion; unexpected sanitizer output cannot pass')
