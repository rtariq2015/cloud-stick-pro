"""Desktop source/SDK-contract reproduction of diag1's recorded boot signature.
No serial, cloud, credentials or physical device use.
"""
from pathlib import Path
import json,os,resource,subprocess
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/os.environ.get('CSP_WIFI_TEST_OUT','artifacts/wifi-start2')
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
old=OUT/'diag1-radio-source.cpp'
old.write_bytes(subprocess.check_output(['git','show','4e83702cd2f984c5de87492a1ebeae59b4dfe576:src/wifi_setup_radio.cpp'],cwd=ROOT))
fixture=OUT/'diag1-boot-reproduction.cpp'
fixture.write_text('''#include "radio_stubs/common.h"
#include <iostream>
std::vector<Registered> callbacks;std::vector<wifi_ap_record_t> scanResults;
uint32_t testClock=100000;std::string failFunction;
bool driverStarted=false,associated=false,dhcpStarted=false,listFreed=true,dropBarrier=false;
bool pendingDhcpReset=false,dhcpStopped=false;
wifi_config_t driverConfig;wifi_ap_record_t associatedAP;esp_netif_ip_info_t lease;
unsigned sdkCalls=0,connectCalls=0,stopCalls=0,storageMode=0;FakeWiFi WiFi;
#include "'''+str(old)+'''"
int main(){
 if(!wifi_setup_radio_init())return 3;
 bool result=wifi_setup_radio_disconnect();const auto& s=wifi_setup_radio_snapshot();
 std::cout<<"{\\"disconnect_ok\\":"<<(result?"true":"false")<<",\\"wifi_fault\\":"
  <<(s.state==WifiSetupRadioState::Fault?"true":"false")<<",\\"wifi_step\\":"
  <<unsigned(s.step)<<",\\"wifi_sdk_error\\":"<<s.sdk_error<<"}\\n";
 return !result&&s.state==WifiSetupRadioState::Fault&&unsigned(s.step)==11&&s.sdk_error==0?42:0;
}
''')
record=json.loads((OUT/'build-record.json').read_text())
cmd=next(r['command'] for r in record['commands'] if r['name']=='radio-build')
cmd=[str(fixture) if x=='test/wifi_integration/radio.test.cpp' else x for x in cmd]
cmd[1:1]=['-Itest/wifi_integration'];cmd[cmd.index('-o')+1]=str(OUT/'diag1-boot-reproduction')
r=subprocess.run(cmd,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=90)
(OUT/'boot-reproduction-build.log').write_text(r.stdout+r.stderr);assert r.returncode==0 and not r.stderr
r=subprocess.run([str(OUT/'diag1-boot-reproduction')],env=ENV,capture_output=True,text=True,timeout=15)
(OUT/'boot-reproduction.log').write_text(r.stdout+r.stderr)
assert r.returncode==42 and not r.stderr
data=json.loads(r.stdout);assert data=={'disconnect_ok':False,'wifi_fault':True,'wifi_step':11,'wifi_sdk_error':0}
(OUT/'boot-reproduction-results.json').write_text(json.dumps({'passed':True,'original_source_commit':'4e83702cd2f984c5de87492a1ebeae59b4dfe576',
 'signature_matches_target_samples':True,'signature':data,'sdk_driver_started_precondition_modeled':True,
 'actual_target_failed_api_return_logged':False,'stderr_empty':True,'hardware':False},indent=2)+'\n')
print('PASS exact diag1 source reproduces observed fault/step11/error0 under strict SDK precondition; corrected boot-disconnect case passes')
