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
 disconnected.store(0);callback_state.store(WifiSetupRadioState::Idle);barrier_ack.store(0);barrier_sequence=started=last_scan=0;
 std::memset(expected_ssid,0,sizeof expected_ssid);station=nullptr;wifi_handler=ip_handler=barrier_handler=nullptr;
}
void ip(){associated=true;associatedAP={};std::memcpy(associatedAP.ssid,driverConfig.sta.ssid,32);associatedAP.authmode=driverConfig.sta.threshold.authmode;
 lease.ip.addr=htonl(0xc0a80164);lease.netmask.addr=htonl(0xffffff00);lease.gw.addr=htonl(0xc0a80101);emit(IP_EVENT,IP_EVENT_STA_GOT_IP);}
int main(){
 using namespace pro::wifi_setup;
 const CheckProfile secured{1,"CSP Test WiFi","synthetic-password",Security::Wpa2Personal};
 reset();CHECK(wifi_setup_radio_init());CHECK(storageMode==WIFI_STORAGE_RAM);CHECK(connectCalls==0);
 CHECK(snapshot.step==WifiSetupRadioStep::Ready);CHECK(snapshot.sdk_error==0);
 // Actual boot restores the selected network through disconnect() first.
 // The SDK's scan-list API must not be called after the driver is stopped.
 CHECK(wifi_setup_radio_disconnect());CHECK(!driverStarted);CHECK(snapshot.sdk_error==0);
 CHECK(wifi_setup_radio_connect(secured));CHECK(dhcpStarted);CHECK(driverConfig.sta.failure_retry_cnt==0);
 CHECK(driverConfig.sta.threshold.authmode==WIFI_AUTH_WPA2_PSK);CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Connecting);
 CHECK(lease.ip.addr==0);ip();wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected);
 CHECK(wifi_setup_radio_snapshot().error==Error::LocalOnly);
 emit(IP_EVENT,IP_EVENT_STA_LOST_IP);wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Failed);CHECK(!driverStarted);
 CHECK(wifi_setup_radio_connect({2,"CSP Test Open","",Security::Open}));CHECK(driverConfig.sta.threshold.authmode==WIFI_AUTH_OPEN);
 ip();wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected);
 // A failed new attempt cannot borrow the old attempt's address or GOT_IP flag.
 CHECK(wifi_setup_radio_connect(secured));CHECK(lease.ip.addr==0);wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Connecting);
 testClock+=25001;associated=true;wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().error==Error::Dhcp);
 CHECK(wifi_setup_radio_connect(secured));wifi_event_sta_disconnected_t wrong{WIFI_REASON_AUTH_FAIL};emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&wrong);
 wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().error==Error::WrongPassword);CHECK(!driverStarted);
 CHECK(wifi_setup_radio_connect(secured));ip();lease.gw.addr=htonl(0x0a000001);wifi_setup_radio_poll();testClock+=25001;wifi_setup_radio_poll();CHECK(wifi_setup_radio_snapshot().error==Error::Dhcp);
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
  CHECK(snapshot.step!=WifiSetupRadioStep::None);CHECK(snapshot.sdk_error!=0);
  const auto calls=sdkCalls;CHECK(!wifi_setup_radio_init());CHECK(calls==sdkCalls);
 }
 for(const char* failureName:{"config","dhcp-start","connect"}){reset();CHECK(wifi_setup_radio_init());failFunction=failureName;CHECK(!wifi_setup_radio_connect(secured));CHECK(wifi_setup_radio_snapshot().state!=WifiSetupRadioState::LocalConnected);}
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
#if CSP_ENABLE_GUEST_DIAGNOSTIC
 reset();CHECK(wifi_setup_radio_init());CHECK(!wifi_setup_radio_scan(nullptr));CHECK(!wifi_setup_radio_scan(""));
 for(unsigned i=0;i<100;++i){wifi_ap_record_t row{};const auto name="Other "+std::to_string(i);std::memcpy(row.ssid,name.c_str(),name.size());scanResults.push_back(row);}
 for(int auth:{5,14}){wifi_ap_record_t row{};std::memcpy(row.ssid,"Guest-Net",9);row.authmode=static_cast<wifi_auth_mode_t>(auth);scanResults.push_back(row);}
 CHECK(wifi_setup_radio_scan("Guest-Net"));CHECK(lastScanTarget=="Guest-Net"&&scanMin==0&&scanMax==120);
 emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE);wifi_setup_radio_poll();
 CHECK(snapshot.count==2&&!snapshot.truncated);CHECK(snapshot.rows[0].authmode==5&&snapshot.rows[1].authmode==14);
 CHECK(!snapshot.rows[0].open&&!snapshot.rows[1].open);
 CHECK(wifi_setup_radio_connect({0,"Guest-Net","",Security::Open}));
 wifi_event_sta_disconnected_t missing{201};emit(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&missing);wifi_setup_radio_poll();
 CHECK(snapshot.state==WifiSetupRadioState::Failed&&snapshot.disconnect_reason==201&&!driverStarted);
 CHECK(wifi_setup_radio_connect({0,"Guest-Net","",Security::Open}));CHECK(snapshot.disconnect_reason==0);
#endif
#if CSP_BENCH_RADIO_RELEASE
 reset();CHECK(wifi_setup_radio_init());CHECK(wifi_setup_radio_connect(secured));ip();wifi_setup_radio_poll();
 CHECK(wifi_setup_radio_release());CHECK(!driverInitialized&&!driverStarted&&!station&&callbacks.empty());
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
