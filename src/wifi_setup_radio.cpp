#include "wifi_setup_radio.h"
#if CSP_ENABLE_WIFI_SETUP
#include "pro_network.h"
#include <wifi_address.h>
#include <WiFi.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstring>
using namespace pro::wifi_setup;
ESP_EVENT_DEFINE_BASE(CSP_WIFI_BARRIER);
namespace {
WifiSetupRadioSnapshot snapshot;
wifi_ap_record_t records[20]{};
std::atomic<bool> scan_done{false},got_ip{false},lost_ip{false};
std::atomic<int> disconnected{0};
std::atomic<WifiSetupRadioState> callback_state{WifiSetupRadioState::Idle};
std::atomic<uint32_t> barrier_ack{0};
uint32_t barrier_sequence=0,started=0,last_scan=0;
bool initialized=false,attempted_scan=false,expected_open=false;
WifiSetupRadioState before_scan=WifiSetupRadioState::Idle;
char expected_ssid[33]{};
esp_netif_t* station=nullptr;
esp_event_handler_instance_t wifi_handler=nullptr,ip_handler=nullptr,barrier_handler=nullptr;
bool checked(esp_err_t code,WifiSetupRadioStep step){snapshot.step=step;snapshot.sdk_error=code;return code==ESP_OK;}
void state(WifiSetupRadioState s){snapshot.state=s;callback_state.store(s,std::memory_order_release);}
void event(void*,esp_event_base_t base,int32_t id,void* payload){
  if(base==CSP_WIFI_BARRIER&&payload){barrier_ack.store(*static_cast<uint32_t*>(payload),std::memory_order_release);return;}
  const auto current=callback_state.load(std::memory_order_acquire);
  if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE&&
     (current==WifiSetupRadioState::Scanning||current==WifiSetupRadioState::Cancelling))scan_done.store(true,std::memory_order_release);
  if(current!=WifiSetupRadioState::Connecting&&current!=WifiSetupRadioState::LocalConnected)return;
  if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)got_ip.store(true,std::memory_order_release);
  if(base==IP_EVENT&&id==IP_EVENT_STA_LOST_IP)lost_ip.store(true,std::memory_order_release);
  if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED&&payload)
    disconnected.store(static_cast<wifi_event_sta_disconnected_t*>(payload)->reason,std::memory_order_release);
}
bool barrier(){
  // Drain queued default handlers before reasserting DHCP STOPPED. IDF's stop
  // handler can reset DHCP to INIT, invalidating an earlier synchronous stop.
  const uint32_t wanted=++barrier_sequence;if(!wanted)return checked(ESP_ERR_INVALID_STATE,WifiSetupRadioStep::BarrierPost);
  if(!checked(esp_event_post(CSP_WIFI_BARRIER,1,&wanted,sizeof wanted,pdMS_TO_TICKS(100)),WifiSetupRadioStep::BarrierPost))return false;
  const uint32_t start=millis();
  while(barrier_ack.load(std::memory_order_acquire)!=wanted){if(uint32_t(millis()-start)>=2000)return checked(ESP_ERR_TIMEOUT,WifiSetupRadioStep::BarrierWait);vTaskDelay(1);}
  checked(ESP_OK,WifiSetupRadioStep::BarrierWait);
  return true;
}
bool dhcpStop(){const auto e=esp_netif_dhcpc_stop(station);checked(e,WifiSetupRadioStep::DhcpStop);return e==ESP_OK||e==ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;}
bool halt(){
  state(WifiSetupRadioState::Idle);got_ip.store(false);lost_ip.store(false);disconnected.store(0);
  esp_wifi_disconnect();const auto stopped=esp_wifi_stop();
  checked(stopped,WifiSetupRadioStep::Stop);
  if(stopped!=ESP_OK&&stopped!=ESP_ERR_WIFI_NOT_STARTED)return false;
  if(!barrier()||!dhcpStop())return false;
  esp_netif_ip_info_t empty{};return checked(esp_netif_set_ip_info(station,&empty),WifiSetupRadioStep::ClearAddress);
}
bool start(){return checked(esp_wifi_start(),WifiSetupRadioStep::Start)&&barrier();}
bool supported(wifi_auth_mode_t auth){return auth==WIFI_AUTH_OPEN||auth==WIFI_AUTH_WPA2_PSK||auth==WIFI_AUTH_WPA_WPA2_PSK||auth==WIFI_AUTH_WPA2_WPA3_PSK;}
void fault(){
  if(snapshot.sdk_error==ESP_OK){snapshot.step=WifiSetupRadioStep::Unattributed;snapshot.sdk_error=ESP_ERR_INVALID_STATE;}
  snapshot.error=Error::Unavailable;state(WifiSetupRadioState::Fault);
}
bool fail(Error error){
  // Explicitly stop the station and DHCP so no core/vendor retry uses an old
  // candidate. A later attempt starts only after the event barrier succeeds.
  if(!halt()){fault();return false;}
  snapshot.error=error;state(WifiSetupRadioState::Failed);return false;
}
}
bool wifi_setup_radio_init(){
  if(initialized)return snapshot.state!=WifiSetupRadioState::Fault;
  initialized=true;WiFi.persistent(false);WiFi.setAutoReconnect(false);
  if(!checked(WiFi.mode(WIFI_STA)?ESP_OK:ESP_FAIL,WifiSetupRadioStep::Mode)||
     !checked(esp_wifi_set_storage(WIFI_STORAGE_RAM),WifiSetupRadioStep::Storage)){fault();return false;}
  station=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");if(!station){checked(ESP_ERR_NOT_FOUND,WifiSetupRadioStep::Station);fault();return false;}
  if(!checked(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,nullptr,&wifi_handler),WifiSetupRadioStep::WifiEvents)||
     !checked(esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID,event,nullptr,&ip_handler),WifiSetupRadioStep::IpEvents)||
     !checked(esp_event_handler_instance_register(CSP_WIFI_BARRIER,1,event,nullptr,&barrier_handler),WifiSetupRadioStep::BarrierEvents)){fault();return false;}
  if(!halt()||!start()){fault();return false;}checked(ESP_OK,WifiSetupRadioStep::Ready);return true;
}
static bool scan(const char* target){
  if(!initialized||snapshot.state==WifiSetupRadioState::Fault||snapshot.state==WifiSetupRadioState::Connecting||
     snapshot.state==WifiSetupRadioState::Cancelling)return false;
  if(snapshot.state==WifiSetupRadioState::Scanning)return true;
  const uint32_t now=millis();if(attempted_scan&&uint32_t(now-last_scan)<15000)return false;
  // Driver may be stopped after a failed/cancelled connection.
  const auto result=esp_wifi_start();if(!checked(result,WifiSetupRadioStep::Start))return false;
  attempted_scan=true;last_scan=started=now;scan_done.store(false);before_scan=snapshot.state;
  if(!checked(esp_wifi_clear_ap_list(),WifiSetupRadioStep::ClearScanList)){fault();return false;}
  wifi_scan_config_t scan{};scan.show_hidden=false;scan.scan_type=WIFI_SCAN_TYPE_ACTIVE;
  scan.scan_time.active.min=30;scan.scan_time.active.max=80;
#if CSP_ENABLE_GUEST_DIAGNOSTIC
  // SDK copies scan configuration; the diagnostic additionally uses a static
  // SSID. Filtering avoids losing a weak target among 20 stronger other SSIDs.
  if(target){scan.ssid=reinterpret_cast<uint8_t*>(const_cast<char*>(target));scan.scan_time.active.min=0;scan.scan_time.active.max=120;}
#else
  (void)target;
#endif
  state(WifiSetupRadioState::Scanning);snapshot.error=Error::None;
  if(!checked(esp_wifi_scan_start(&scan,false),WifiSetupRadioStep::StartScan)){
    if(!checked(esp_wifi_clear_ap_list(),WifiSetupRadioStep::ClearScanList)){fault();return false;}
    snapshot.error=Error::Unavailable;state(WifiSetupRadioState::Failed);return false;
  }return true;
}
bool wifi_setup_radio_scan(){return scan(nullptr);}
#if CSP_ENABLE_GUEST_DIAGNOSTIC
bool wifi_setup_radio_scan(const char* target){return target&&validSSID(target)&&scan(target);}
#endif
bool wifi_setup_radio_connect(const CheckProfile& p){
  if(!initialized||snapshot.state==WifiSetupRadioState::Fault||snapshot.state==WifiSetupRadioState::Scanning||
     snapshot.state==WifiSetupRadioState::Cancelling||!validSSID(p.ssid)||
     (p.security==Security::Open?!p.secret.empty():p.security!=Security::Wpa2Personal||!validPassword(p.secret)))return false;
  if(!halt()){fault();return false;}
  wifi_config_t config{};std::memcpy(config.sta.ssid,p.ssid.data(),p.ssid.size());
  if(!p.secret.empty())std::memcpy(config.sta.password,p.secret.data(),p.secret.size());
  config.sta.threshold.authmode=p.security==Security::Open?WIFI_AUTH_OPEN:WIFI_AUTH_WPA2_PSK;
  config.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;config.sta.failure_retry_cnt=0;
  const auto configured=esp_wifi_set_config(WIFI_IF_STA,&config);
  eraseSecretMemory(&config,sizeof config);
  if(!checked(configured,WifiSetupRadioStep::Configure)||!start()){fault();return false;}
  expected_ssid[0]=0;std::memset(expected_ssid,0,sizeof expected_ssid);std::memcpy(expected_ssid,p.ssid.data(),p.ssid.size());
  expected_open=p.security==Security::Open;got_ip.store(false);disconnected.store(0);
  auto dhcp=esp_netif_dhcpc_start(station);
  checked(dhcp,WifiSetupRadioStep::DhcpStart);
  if(dhcp!=ESP_OK&&dhcp!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)return fail(Error::Dhcp);
  snapshot.error=Error::None;snapshot.profile=p.id;started=millis();state(WifiSetupRadioState::Connecting);
#if CSP_ENABLE_GUEST_DIAGNOSTIC
  snapshot.disconnect_reason=0;
#endif
  if(!checked(esp_wifi_connect(),WifiSetupRadioStep::Connect))return fail(Error::Unavailable);return true;
}
bool wifi_setup_radio_disconnect(){
  if(!initialized)return true;
  if(snapshot.state==WifiSetupRadioState::Fault)return false;
  if(snapshot.state==WifiSetupRadioState::Scanning||snapshot.state==WifiSetupRadioState::Cancelling){
    state(WifiSetupRadioState::Cancelling);
    if(!checked(esp_wifi_scan_stop(),WifiSetupRadioStep::StopScan)||
       !checked(esp_wifi_clear_ap_list(),WifiSetupRadioStep::ClearScanList)){fault();return false;}
    // A synchronous station stop plus DHCP/event drain retires the old scan.
  }
  if(!halt()){fault();return false;}
  // Every completed/failed/cancelled scan clears its driver list while Wi-Fi is
  // running. Do not call the list API after halt(): the SDK rejects a stopped
  // driver even when no list exists (including the first boot restore path).
  scan_done.store(false);snapshot.error=Error::None;return true;
}
#if CSP_RADIO_RELEASE_ENABLED
bool wifi_setup_radio_release(){
  if(!initialized)return true;
  if(snapshot.state==WifiSetupRadioState::Fault)return false;
  if(!wifi_setup_radio_disconnect())return false;
  // Direct SDK stop does not update Arduino's started bookkeeping. Reassert
  // its running state before the supported OFF path stops/deinitializes it.
  // start() alone does not request association; reconnect remains explicit.
  if(!checked(esp_wifi_start(),WifiSetupRadioStep::ReleaseStart)||
     !checked(WiFi.mode(WIFI_OFF)?ESP_OK:ESP_FAIL,WifiSetupRadioStep::ReleaseMode)){
    fault();return false;
  }
  // Do not call DHCP/address APIs through the now-destroyed netif. Drain the
  // default event loop, prove driver deinit, then unregister our own handlers.
  station=nullptr;
  if(!barrier()){fault();return false;}
  wifi_mode_t mode=WIFI_MODE_NULL;
  const auto proof=esp_wifi_get_mode(&mode);
  checked(proof==ESP_ERR_WIFI_NOT_INIT?ESP_OK:ESP_ERR_INVALID_STATE,WifiSetupRadioStep::ReleaseProof);
  if(proof!=ESP_ERR_WIFI_NOT_INIT){fault();return false;}
  if(!checked(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler),WifiSetupRadioStep::ReleaseWifiHandler)||
     !checked(esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,ip_handler),WifiSetupRadioStep::ReleaseIpHandler)||
     !checked(esp_event_handler_instance_unregister(CSP_WIFI_BARRIER,1,barrier_handler),WifiSetupRadioStep::ReleaseBarrierHandler)){
    fault();return false;
  }
  wifi_handler=ip_handler=barrier_handler=nullptr;initialized=false;
  attempted_scan=false;scan_done.store(false);got_ip.store(false);lost_ip.store(false);disconnected.store(0);
  std::memset(expected_ssid,0,sizeof expected_ssid);snapshot={};state(WifiSetupRadioState::Idle);
  return true;
}
#endif
void wifi_setup_radio_poll(){
  if(snapshot.state==WifiSetupRadioState::Scanning){
    if(scan_done.exchange(false,std::memory_order_acq_rel)){
      uint16_t total=0,count=20;snapshot.count=0;
      if(!checked(esp_wifi_scan_get_ap_num(&total),WifiSetupRadioStep::ScanCount)||
         (total&&!checked(esp_wifi_scan_get_ap_records(&count,records),WifiSetupRadioStep::ScanRecords))){
        if(!checked(esp_wifi_clear_ap_list(),WifiSetupRadioStep::ClearScanList)){fault();return;}
        snapshot.error=Error::Unavailable;state(WifiSetupRadioState::Failed);return;
      }
      if(!total)count=0;
      if(!checked(esp_wifi_clear_ap_list(),WifiSetupRadioStep::ClearScanList)){fault();return;}
      snapshot.truncated=total>20;
      if(count>20)count=20;
      for(size_t i=0;i<count;++i){
        const size_t n=strnlen(reinterpret_cast<const char*>(records[i].ssid),33);
        if(!validSSID({reinterpret_cast<const char*>(records[i].ssid),n}))continue;
        bool duplicate=false;for(size_t j=0;j<snapshot.count;++j)
          if(snapshot.rows[j].open==(records[i].authmode==WIFI_AUTH_OPEN)&&
             !std::strcmp(snapshot.rows[j].ssid,reinterpret_cast<const char*>(records[i].ssid))&&
             snapshot.rows[j].supported==supported(records[i].authmode)
#if CSP_ENABLE_GUEST_DIAGNOSTIC
             &&snapshot.rows[j].authmode==uint8_t(records[i].authmode)
#endif
             )duplicate=true;
        if(duplicate)continue;auto& row=snapshot.rows[snapshot.count++];row={};
        std::memcpy(row.ssid,records[i].ssid,n);row.rssi=records[i].rssi;row.channel=records[i].primary;
        row.open=records[i].authmode==WIFI_AUTH_OPEN;row.supported=supported(records[i].authmode);
#if CSP_ENABLE_GUEST_DIAGNOSTIC
        row.authmode=uint8_t(records[i].authmode);
#endif
      }
      std::memset(records,0,sizeof records);snapshot.has_scan=true;
      state(before_scan==WifiSetupRadioState::LocalConnected?WifiSetupRadioState::LocalConnected:WifiSetupRadioState::Idle);
    }else if(uint32_t(millis()-started)>=10000){
      if(wifi_setup_radio_disconnect()){snapshot.error=Error::Unavailable;state(WifiSetupRadioState::Failed);}
    }
  }else if(snapshot.state==WifiSetupRadioState::Connecting||snapshot.state==WifiSetupRadioState::LocalConnected){
    const bool was_connected=snapshot.state==WifiSetupRadioState::LocalConnected;
    const int reason=disconnected.exchange(0,std::memory_order_acq_rel);
#if CSP_ENABLE_GUEST_DIAGNOSTIC
    if(reason)snapshot.disconnect_reason=reason;
#endif
    if(reason){fail(reason==WIFI_REASON_AUTH_FAIL||reason==WIFI_REASON_HANDSHAKE_TIMEOUT||reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT?Error::WrongPassword:Error::Unavailable);return;}
    if(lost_ip.exchange(false,std::memory_order_acq_rel)){fail(Error::Dhcp);return;}
    if(got_ip.load(std::memory_order_acquire)){
      wifi_ap_record_t ap{};esp_netif_ip_info_t address{};
      if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&esp_netif_get_ip_info(station,&address)==ESP_OK&&
         strnlen(reinterpret_cast<const char*>(ap.ssid),sizeof ap.ssid)==std::strlen(expected_ssid)&&
         !std::memcmp(expected_ssid,ap.ssid,std::strlen(expected_ssid))&&supported(ap.authmode)&&
         (ap.authmode==WIFI_AUTH_OPEN)==expected_open&&
         validWifiLease(ntohl(address.ip.addr),ntohl(address.netmask.addr),ntohl(address.gw.addr))){
        snapshot.error=Error::LocalOnly;state(WifiSetupRadioState::LocalConnected);return;
      }
    }
    if(was_connected){fail(Error::Dhcp);return;}
    if(uint32_t(millis()-started)>=25000){wifi_ap_record_t ap{};fail(esp_wifi_sta_get_ap_info(&ap)==ESP_OK?Error::Dhcp:Error::Unavailable);}
  }
}
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot(){return snapshot;}
#endif
