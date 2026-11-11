#include "radio_stubs/common.h"
#include <cassert>
#include <iostream>
std::vector<Registered> callbacks;std::vector<wifi_ap_record_t> scanResults;
uint32_t testClock=100000;std::string failFunction;
bool driverStarted=false,associated=false,dhcpStarted=false,listFreed=true,dropBarrier=false;
bool pendingDhcpReset=false,dhcpStopped=false;
wifi_config_t driverConfig;wifi_ap_record_t associatedAP;esp_netif_ip_info_t lease;
unsigned sdkCalls=0,connectCalls=0,stopCalls=0,storageMode=0;
FakeWiFi WiFi;
#include "../../src/wifi_setup_radio.cpp"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::abort();}}while(0)
void reset(){
 callbacks.clear();scanResults.clear();testClock=100000;failFunction.clear();driverStarted=associated=dhcpStarted=dropBarrier=false;
 pendingDhcpReset=dhcpStopped=false;
 listFreed=true;sdkCalls=connectCalls=stopCalls=storageMode=0;driverConfig={};associatedAP={};lease={};
 initialized=attempted_scan=driverInitialized=false;expected_open=false;snapshot={};scan_done.store(false);got_ip.store(false);lost_ip.store(false);
 delayedBeacon=false;scanConfig={};
#if CSP_ENABLE_WIFI_ATTEMPT_TRACE
 attempt={};last_failure={};attempt_started=attempt_sequence=0;
#endif
 disconnected.store(0);callback_state.store(WifiSetupRadioState::Idle);barrier_ack.store(0);barrier_sequence=started=last_scan=0;
 std::memset(expected_ssid,0,sizeof expected_ssid);station=nullptr;wifi_handler=ip_handler=barrier_handler=nullptr;
}
void ip(){associated=true;associatedAP={};std::memcpy(associatedAP.ssid,driverConfig.sta.ssid,32);associatedAP.authmode=driverConfig.sta.threshold.authmode;
 lease.ip.addr=htonl(0xc0a80164);lease.netmask.addr=htonl(0xffffff00);lease.gw.addr=htonl(0xc0a80101);emit(IP_EVENT,IP_EVENT_STA_GOT_IP);}
int main(){
 using namespace pro::wifi_setup;
 const CheckProfile secured{1,"CSP Test WiFi","synthetic-password",Security::Wpa2Personal};
 reset();CHECK(wifi_setup_radio_init());CHECK(storageMode==WIFI_STORAGE_RAM);CHECK(connectCalls==0);
 CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(snapshot.step==WifiSetupRadioStep::Ready);CHECK(snapshot.sdk_error==0);
 // Actual boot restores the selected network through disconnect() first.
 // The SDK's scan-list API must not be called after the driver is stopped.
 CHECK(wifi_setup_radio_disconnect());CHECK(!driverStarted);CHECK(snapshot.sdk_error==0);
 CHECK(wifi_setup_radio_connect(secured));CHECK(dhcpStarted);CHECK(driverConfig.sta.failure_retry_cnt==0);
 CHECK(driverConfig.sta.threshold.authmode==WIFI_AUTH_WPA2_PSK);CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Connecting);
 CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(lease.ip.addr==0);ip();wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected);
 CHECK(wifi_setup_radio_snapshot().error==Error::LocalOnly);
 CHECK(!std::strcmp(wifi_setup_radio_connected_ssid(),"CSP Test WiFi"));
 emit(IP_EVENT,IP_EVENT_STA_LOST_IP);wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Failed);CHECK(!driverStarted);
 CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(wifi_setup_radio_connect({2,"CSP Test Open","",Security::Open}));CHECK(driverConfig.sta.threshold.authmode==WIFI_AUTH_OPEN);
 ip();wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected);
 CHECK(!std::strcmp(wifi_setup_radio_connected_ssid(),"CSP Test Open"));
 // A failed new attempt cannot borrow the old attempt's address or GOT_IP flag.
 CHECK(wifi_setup_radio_connect(secured));CHECK(lease.ip.addr==0);wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Connecting);
 CHECK(!*wifi_setup_radio_connected_ssid());
 testClock+=25001;associated=true;wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().error==Error::Dhcp);
 CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(wifi_setup_radio_connect(secured));wifi_event_sta_disconnected_t wrong{WIFI_REASON_AUTH_FAIL};emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&wrong);
 wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().error==Error::WrongPassword);CHECK(!driverStarted);
 CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(wifi_setup_radio_connect(secured));ip();lease.gw.addr=htonl(0x0a000001);wifi_setup_radio_poll();testClock+=25001;wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().error==Error::Dhcp);
 CHECK(!*wifi_setup_radio_connected_ssid());
 // An attempted or selected name is never displayed without the real AP proof.
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_connect(secured));ip();
 std::memset(associatedAP.ssid,0,sizeof associatedAP.ssid);std::memcpy(associatedAP.ssid,"Other AP",8);
 wifi_setup_radio_poll();CHECK(snapshot.state==WifiSetupRadioState::Connecting);CHECK(!*wifi_setup_radio_connected_ssid());
 testClock+=25001;wifi_setup_radio_poll();CHECK(snapshot.state==WifiSetupRadioState::Failed);CHECK(!*wifi_setup_radio_connected_ssid());
 // A fallback reports the network actually admitted, not the earlier profile.
 CHECK(wifi_setup_radio_connect({7,"Guest-Net","",Security::Open}));CHECK(!*wifi_setup_radio_connected_ssid());
 ip();wifi_setup_radio_poll();CHECK(snapshot.profile==7);CHECK(!std::strcmp(wifi_setup_radio_connected_ssid(),"Guest-Net"));
 // Losing the associated AP/name proof retires the displayed name immediately.
 std::memset(associatedAP.ssid,0,sizeof associatedAP.ssid);std::memcpy(associatedAP.ssid,"Other AP",8);
 wifi_setup_radio_poll();CHECK(snapshot.state==WifiSetupRadioState::Failed);CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(wifi_setup_radio_connect({8,"12345678901234567890123456789012","",Security::Open}));
 ip();wifi_setup_radio_poll();CHECK(std::strlen(wifi_setup_radio_connected_ssid())==32);
 CHECK(!std::strcmp(wifi_setup_radio_connected_ssid(),"12345678901234567890123456789012"));
 CHECK(wifi_setup_radio_disconnect());CHECK(!*wifi_setup_radio_connected_ssid());
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_scan());
 for(unsigned i=0;i<25;++i){wifi_ap_record_t row{};auto name=std::string("CSP ")+std::to_string(i);std::memcpy(row.ssid,name.data(),name.size());row.rssi=-40-i;scanResults.push_back(row);}
 scanResults[0].authmode=WIFI_AUTH_OPEN;scanResults[1].authmode=WIFI_AUTH_ENTERPRISE;scanResults[2].authmode=WIFI_AUTH_WPA3_PSK;
 std::memcpy(scanResults[3].ssid,"\xc0\x80",3);emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE);wifi_setup_radio_poll();
 CHECK(listFreed);CHECK(wifi_setup_radio_snapshot().truncated);CHECK(wifi_setup_radio_snapshot().count==19);
 CHECK(wifi_setup_radio_snapshot().rows[0].open);CHECK(!wifi_setup_radio_snapshot().rows[1].supported);CHECK(!wifi_setup_radio_snapshot().rows[2].supported);
 CHECK(!wifi_setup_radio_scan());testClock+=15001;CHECK(wifi_setup_radio_scan());CHECK(wifi_setup_radio_disconnect());CHECK(listFreed);
 testClock+=15001;CHECK(wifi_setup_radio_scan());testClock+=10001;wifi_setup_radio_poll();CHECK(listFreed);CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Failed);
 for(const char* failureName:{"mode","storage","netif","register","post","stop","dhcp-stop","set-ip","start"}){
  reset();failFunction=failureName;CHECK(!wifi_setup_radio_init());CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Fault);
  CHECK(!*wifi_setup_radio_connected_ssid());
  CHECK(snapshot.step!=WifiSetupRadioStep::None);CHECK(snapshot.sdk_error!=0);
  const auto calls=sdkCalls;CHECK(!wifi_setup_radio_init());CHECK(calls==sdkCalls);
 }
 for(const char* failureName:{"config","dhcp-start","connect"}){reset();CHECK(wifi_setup_radio_init());failFunction=failureName;CHECK(!wifi_setup_radio_connect(secured));CHECK(wifi_setup_radio_snapshot().state!=WifiSetupRadioState::LocalConnected);CHECK(!*wifi_setup_radio_connected_ssid());}
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_scan());failFunction="clear-list";
 CHECK(!wifi_setup_radio_disconnect());CHECK(snapshot.state==WifiSetupRadioState::Fault);
 CHECK(snapshot.step==WifiSetupRadioStep::ClearScanList);CHECK(snapshot.sdk_error==ESP_FAIL);
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_scan());failFunction="scan-stop";
 CHECK(!wifi_setup_radio_disconnect());CHECK(snapshot.state==WifiSetupRadioState::Fault);
 CHECK(snapshot.step==WifiSetupRadioStep::StopScan);CHECK(snapshot.sdk_error==ESP_FAIL);
 reset();CHECK(wifi_setup_radio_init());dropBarrier=true;CHECK(!wifi_setup_radio_connect(secured));CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Fault);
 CHECK(validWifiLease(0xc0a80164,0xffffff00,0xc0a80101));
 CHECK(!validWifiLease(0xc0a80100,0xffffff00,0xc0a80101));CHECK(!validWifiLease(0xc0a801ff,0xffffff00,0xc0a80101));
 CHECK(!validWifiLease(0xc0a80164,0xffffff00,0x0a000001));CHECK(!validWifiLease(0xc0a80164,0xffffffff,0xc0a80164));
 // A beacon-only AP must remain discoverable across an ordinary beacon interval.
 reset();CHECK(wifi_setup_radio_init());delayedBeacon=true;CHECK(wifi_setup_radio_scan());
 emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE);wifi_setup_radio_poll();
#if CSP_ENABLE_WIFI_STANDARD_SCAN
 CHECK(snapshot.count==1);CHECK(std::string(snapshot.rows[0].ssid)=="CSP Beacon Only");
#else
 CHECK(snapshot.count==0); // historical profiles preserve their exact timing
#endif
#if CSP_ENABLE_WIFI_ATTEMPT_TRACE
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_connect(secured));
 testClock+=7108;wifi_event_sta_disconnected_t missing{201};emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&missing);
 wifi_setup_radio_poll();
 const auto failed=wifi_setup_radio_last_failure();
 CHECK(failed.sequence==1&&failed.elapsed_ms==7108);CHECK(failed.disconnect_reason==201);
 CHECK(failed.outcome==WifiAttemptOutcome::Failed&&failed.error==Error::Unavailable);
 CHECK(failed.step==WifiSetupRadioStep::Connect&&failed.sdk_error==ESP_OK);
 CHECK(snapshot.step==WifiSetupRadioStep::ClearAddress); // live cleanup really overwrote it
 CHECK(wifi_setup_radio_disconnect());CHECK(wifi_setup_radio_connect(secured));ip();wifi_setup_radio_poll();
 CHECK(wifi_setup_radio_attempt().sequence==2);CHECK(wifi_setup_radio_attempt().outcome==WifiAttemptOutcome::Connected);
 CHECK(wifi_setup_radio_attempt().flags==63);CHECK(wifi_setup_radio_last_failure().sequence==failed.sequence);
 CHECK(wifi_setup_radio_last_failure().disconnect_reason==201);
 CHECK(wifi_setup_radio_release());CHECK(wifi_setup_radio_init());
 CHECK(wifi_setup_radio_last_failure().sequence==failed.sequence); // survives fallback teardown/reinit
 CHECK(wifi_setup_radio_connect(secured));ip();lease.gw.addr=htonl(0x0a000001);wifi_setup_radio_poll();
 testClock+=25001;wifi_setup_radio_poll();
 CHECK(wifi_setup_radio_last_failure().error==Error::Dhcp);CHECK(wifi_setup_radio_last_failure().flags==31);
 CHECK(wifi_setup_radio_last_failure().disconnect_reason==0);
 const auto dhcp_failure=wifi_setup_radio_last_failure();
 CHECK(wifi_setup_radio_connect(secured));testClock+=17;CHECK(wifi_setup_radio_disconnect());
 CHECK(wifi_setup_radio_attempt().outcome==WifiAttemptOutcome::Cancelled);
 CHECK(wifi_setup_radio_last_failure().sequence==dhcp_failure.sequence);
 CHECK(wifi_setup_radio_connect(secured));failFunction="connect";
 // Inject SDK connect failure on a new attempt; halt must not replace ESP_FAIL.
 CHECK(!wifi_setup_radio_connect(secured));CHECK(wifi_setup_radio_last_failure().sdk_error==ESP_FAIL);
 CHECK(wifi_setup_radio_last_failure().step==WifiSetupRadioStep::Connect);
 failFunction.clear();CHECK(wifi_setup_radio_connect(secured));emit(IP_EVENT,IP_EVENT_STA_LOST_IP);wifi_setup_radio_poll();
 CHECK(wifi_setup_radio_last_failure().flags==WifiAttemptLostIP);
 // The uint32 clock can wrap while connecting.
 testClock=0xfffffff0U;CHECK(wifi_setup_radio_connect(secured));testClock+=32;
 emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&wrong);wifi_setup_radio_poll();
 CHECK(wifi_setup_radio_last_failure().elapsed_ms==32);CHECK(wifi_setup_radio_last_failure().error==Error::WrongPassword);
#endif
#if CSP_BENCH_RADIO_RELEASE
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_connect(secured));ip();wifi_setup_radio_poll();
 CHECK(wifi_setup_radio_release());CHECK(!driverInitialized&&!driverStarted&&!station&&callbacks.empty());
 CHECK(!*wifi_setup_radio_connected_ssid());
 CHECK(wifi_setup_radio_init());CHECK(callbacks.size()==3);CHECK(wifi_setup_radio_connect(secured));
 CHECK(wifi_setup_radio_release());CHECK(callbacks.empty());
 for(const char* failureName:{"start","mode-off","release-proof","unregister","post"}){
  reset();CHECK(wifi_setup_radio_init());failFunction=failureName;
  CHECK(!wifi_setup_radio_release());CHECK(snapshot.state==WifiSetupRadioState::Fault);
  const auto calls=sdkCalls;CHECK(!wifi_setup_radio_init());CHECK(sdkCalls==calls);
 }
#endif
 std::cout<<"PASS "<<checks<<" actual radio-source cases; SDK/events/DHCP simulated, no hardware\n";
}
