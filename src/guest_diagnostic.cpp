#include "guest_diagnostic.h"
#if CSP_ENABLE_GUEST_DIAGNOSTIC
#include "guest_internet_probe.h"
#include "wifi_setup_radio.h"
#include "usb_storage.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <cstdio>
#include <cstring>
namespace pro::guest_diag {
namespace {
constexpr char Target[]="Guest-Net";
constexpr unsigned MaxAttempts=2;
enum class Phase {Idle,Scanning,Connecting,Probing,Cancelling,Complete,Failed,Cancelled};
Phase phase=Phase::Idle;
InternetResult internet;
const char* detail="Send GUEST_TEST_START; uploads disabled in this build";
unsigned attempts=0;
int auth=-1,rssi=0;
bool visible=false;
unsigned last_step=0,last_error=0;
long last_sdk=0;
int last_reason=0;
IPAddress address,gateway,dns;
char lcd[96]="Guest Wi-Fi test\nUploads paused";
const char* phaseName(){
  switch(phase){case Phase::Idle:return "idle";case Phase::Scanning:return "scanning";
  case Phase::Connecting:return "connecting";case Phase::Probing:return "probing";
  case Phase::Cancelling:return "cancelling";case Phase::Complete:return "complete";
  case Phase::Failed:return "failed";case Phase::Cancelled:return "cancelled";}
  return "failed";
}
const char* security(){
  switch(auth){case -1:return "not observed";case 0:return "Open";case 1:return "WEP";
  case 2:return "WPA-Personal";case 3:return "WPA2-Personal";case 4:return "WPA/WPA2-Personal";
  case 5:return "WPA2-Enterprise";case 6:return "WPA3-Personal";case 7:return "WPA2/WPA3-Personal";
  case 8:return "WAPI-Personal";case 9:return "OWE";case 10:return "WPA3-Enterprise-192";
  case 11:return "WPA3-Extended-Personal";case 12:return "WPA3/Mixed-Extended-Personal";
  case 13:return "DPP";case 14:return "WPA3-Enterprise";case 15:return "WPA2/WPA3-Enterprise";
  case 16:return "WPA-Enterprise";case -2:return "conflicting security";default:return "Unknown (see auth code)";}
}
bool possiblePortal(){return (internet.http_status>0&&internet.http_status!=204)||
  (internet.https_status>=300&&internet.https_status<400);}
bool passed(){return internet.http_status==204&&internet.https_verified&&internet.https_status==200;}
bool currentLink(){return WiFi.status()==WL_CONNECTED&&!std::strcmp(WiFi.SSID().c_str(),Target)&&WiFi.localIP()==address;}
bool available(){return phase==Phase::Complete&&passed()&&currentLink();}
void finish(Phase p,const char* why){phase=p;detail=why;
  const auto& radio=wifi_setup_radio_snapshot();last_step=unsigned(radio.step);last_error=unsigned(radio.error);last_sdk=radio.sdk_error;
  last_reason=radio.disconnect_reason;
  std::snprintf(lcd,sizeof lcd,"%s\nRSSI %d dBm\n%s",p==Phase::Complete?"Connected Guest-Net":p==Phase::Cancelled?"Guest test cancelled":"Guest-Net Failed",rssi,
    available()?"Internet Available":possiblePortal()?"Login may be required":"Internet unverified");report();}
bool connect(){
  ++attempts;detail="Connecting to Guest-Net (open); waiting for DHCP";
  const pro::wifi_setup::CheckProfile profile{0,Target,"",pro::wifi_setup::Security::Open};
  if(!wifi_setup_radio_connect(profile)){finish(Phase::Failed,"Cannot start open connection; inspect radio error");return false;}
  phase=Phase::Connecting;std::snprintf(lcd,sizeof lcd,"Connecting Guest-Net\nRSSI %d dBm\nAttempt %u of %u",rssi,attempts,MaxAttempts);report();return true;
}
}
bool busy(){return phase==Phase::Scanning||phase==Phase::Connecting||phase==Phase::Probing||phase==Phase::Cancelling||internetBusy();}
bool start(bool admitted){
  if(!admitted||busy()){USBSerial.println("GUEST_TEST rejected: setup, cleanup or network operation is busy");return false;}
  internet={};attempts=0;auth=-1;rssi=0;visible=false;address=IPAddress();gateway=IPAddress();dns=IPAddress();
  if(!wifi_setup_radio_disconnect()||!wifi_setup_radio_scan(Target)){
    finish(Phase::Failed,"Cannot scan now; radio fault or 15-second scan cooldown");return false;}
  phase=Phase::Scanning;detail="Scanning 2.4 GHz for Guest-Net";
  std::snprintf(lcd,sizeof lcd,"Searching Guest-Net\n2.4 GHz Wi-Fi\nUploads paused");report();return true;
}
void cancel(){
  if(!busy()){USBSerial.println("GUEST_TEST no active diagnostic; setup unchanged");return;}
  if(internetBusy()){internetCancel();phase=Phase::Cancelling;detail="Waiting for network task to unwind safely";return;}
  if(!wifi_setup_radio_disconnect()){finish(Phase::Failed,"Radio could not stop safely");return;}
  finish(Phase::Cancelled,"Diagnostic stopped; saved Wi-Fi settings unchanged");
}
void poll(){
  const auto& radio=wifi_setup_radio_snapshot();
  if(phase==Phase::Scanning&&radio.state!=WifiSetupRadioState::Scanning){
    if(radio.state==WifiSetupRadioState::Failed||radio.state==WifiSetupRadioState::Fault){finish(Phase::Failed,"Wi-Fi scan failed or timed out");return;}
    for(unsigned i=0;i<radio.count;++i){const auto& row=radio.rows[i];if(std::strcmp(row.ssid,Target))continue;
      if(!visible){visible=true;auth=row.authmode;rssi=row.rssi;}
      else if(auth!=row.authmode){auth=-2;finish(Phase::Failed,"Conflicting security types advertise Guest-Net; no connection attempted");return;}
    }
    if(!visible){finish(Phase::Failed,"Guest-Net not visible on 2.4 GHz; move closer or check AP band");return;}
    // A truncated scan cannot establish that all matching APs are open.
    if(radio.truncated){finish(Phase::Failed,"Matching scan truncated; security cannot be established safely");return;}
    if(auth!=0){finish(Phase::Failed,"Authentication required; use existing provisioning with the reported security type");return;}
    connect();
  }else if(phase==Phase::Connecting){
    if(radio.state==WifiSetupRadioState::Failed){
      if(attempts<MaxAttempts)connect();else finish(Phase::Failed,"Connection/DHCP failed after two attempts; inspect radio error");
    }else if(radio.state==WifiSetupRadioState::Fault)finish(Phase::Failed,"Radio fault; no further attempt");
    else if(radio.state==WifiSetupRadioState::LocalConnected){
      esp_netif_ip_info_t lease{};
      if(!WiFi.STA.netif()||esp_netif_get_ip_info(WiFi.STA.netif(),&lease)!=ESP_OK){finish(Phase::Failed,"Cannot read DHCP lease");return;}
      address=IPAddress(lease.ip.addr);gateway=IPAddress(lease.gw.addr);dns=WiFi.dnsIP(0);
      if(!uint32_t(dns))dns=WiFi.dnsIP(1);
      rssi=WiFi.RSSI();
      if(!internetStart()){finish(Phase::Failed,"Cannot allocate internet test task safely");return;}
      phase=Phase::Probing;detail="Connected; checking DNS, HTTP and verified HTTPS";
      std::snprintf(lcd,sizeof lcd,"Connected Guest-Net\nRSSI %d dBm\nTesting internet",rssi);report();
    }
  }else if(phase==Phase::Probing||phase==Phase::Cancelling){
    if(internetReap(internet)){
      if(phase==Phase::Cancelling){cancel();return;}
      if(radio.state!=WifiSetupRadioState::LocalConnected||std::strcmp(WiFi.SSID().c_str(),Target)||WiFi.localIP()!=address){
        finish(Phase::Failed,"Wi-Fi changed during test; results are not valid for Guest-Net");return;}
      finish(Phase::Complete,passed()?"Internet available; no files uploaded":possiblePortal()?"Possible captive portal; no redirect or terms accepted":
        !internet.clock_valid?"HTTPS not attempted: clock not synchronized":"Internet not verified; inspect DNS, HTTP and TLS results");
    }
  }
}
const char* screen(){
  if(phase==Phase::Complete&&!currentLink())
    return "Wi-Fi changed\nLast test is old\nUploads paused";
  return lcd;
}
void report(){
  const auto& radio=wifi_setup_radio_snapshot();
  USBSerial.printf("GUEST_TEST phase=%s ssid=Guest-Net visible=%s security=%s auth=%d rssi=%d attempt=%u/%u\n",phaseName(),visible?"yes":"no",security(),auth,rssi,attempts,MaxAttempts);
  USBSerial.printf("  ip=%s gateway=%s dns_server=%s dns_http=%s dns_https=%s\n",address.toString().c_str(),gateway.toString().c_str(),dns.toString().c_str(),internet.dns_http?"ok":"not verified",internet.dns_https?"ok":"not verified");
  USBSerial.printf("  http=%d http_error=%d https=%d tls_verified=%s https_error=%d tls_error=%d clock=%s\n",internet.http_status,internet.http_error,internet.https_status,internet.https_verified?"yes":"no",internet.https_error,internet.tls_error,internet.clock_valid?"valid":"not synchronized");
  const bool terminal=phase==Phase::Complete||phase==Phase::Failed||phase==Phase::Cancelled;
  const bool stale=phase==Phase::Complete&&!currentLink();
  USBSerial.printf("  captive_portal=%s internet=%s radio_step=%u sdk_error=%ld radio_error=%u disconnect_reason=%d uploads=disabled\n  %s\n",stale?"unknown":possiblePortal()?"possible":available()?"not detected":"unknown",available()?"available":"unverified",terminal?last_step:unsigned(radio.step),terminal?last_sdk:(long)radio.sdk_error,terminal?last_error:unsigned(radio.error),terminal?last_reason:radio.disconnect_reason,stale?"Wi-Fi changed; stored test results are historical":detail);
}
}
#endif
