#include "guest_diagnostic.h"
#include "guest_internet_probe.h"
#include "wifi_setup_radio.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

NativeSerial USBSerial;
NativeWiFi WiFi;
namespace {
WifiSetupRadioSnapshot radio;
bool disconnect_ok=true,scan_ok=true,connect_ok=true,probe_start_ok=true;
bool probe_busy=false,probe_ready=false;
int disconnect_calls=0,scan_calls=0,connect_calls=0,probe_starts=0,probe_cancels=0,reap_calls=0;
int lease_error=ESP_OK;
pro::guest_diag::InternetResult probe_result;
void has(const char* needle){assert(USBSerial.output.find(needle)!=std::string::npos);}
void lacks(const char* needle){assert(USBSerial.output.find(needle)==std::string::npos);}
void row(unsigned auth=0,const char* name="Guest-Net",unsigned index=0){
  assert(index<20);auto& target=radio.rows[index];
  std::strcpy(target.ssid,name);target.authmode=auth;target.rssi=-62;
  target.open=auth==0;target.supported=auth==0||auth==3;
  radio.count=index+1;
}
void scanning(){assert(pro::guest_diag::start(true));assert(pro::guest_diag::busy());
  assert(disconnect_calls==1&&scan_calls==1);assert(radio.state==WifiSetupRadioState::Scanning);}
void connected(){scanning();row();radio.state=WifiSetupRadioState::Idle;
  pro::guest_diag::poll();assert(connect_calls==1);
  radio.state=WifiSetupRadioState::LocalConnected;WiFi.connection_status=WL_CONNECTED;pro::guest_diag::poll();
  assert(probe_starts==1&&probe_busy&&pro::guest_diag::busy());has("phase=probing");}
void goodResult(){probe_result.dns_http=true;probe_result.dns_https=true;
  probe_result.clock_valid=true;probe_result.http_status=204;
  probe_result.https_status=200;probe_result.https_verified=true;}
void finishProbe(){probe_ready=true;USBSerial.output.clear();pro::guest_diag::poll();
  assert(!probe_busy&&!pro::guest_diag::busy());}
void stopped(){assert(!pro::guest_diag::busy());assert(connect_calls==0&&probe_starts==0);has("phase=failed");}
void scenario(const std::string& name){
  using namespace pro::guest_diag;
  if(name=="denied"){
    assert(!start(false));assert(!busy());assert(!disconnect_calls&&!scan_calls&&!connect_calls);return;
  }
  if(name=="cancel_idle"){
    cancel();assert(!busy()&&!disconnect_calls&&!scan_calls&&!connect_calls&&!probe_cancels);
    has("no active diagnostic; setup unchanged");return;
  }
  if(name=="busy_reject"){
    scanning();assert(!start(true));assert(disconnect_calls==1&&scan_calls==1);return;
  }
  if(name=="disconnect_failure"){
    disconnect_ok=false;assert(!start(true));assert(!scan_calls);stopped();return;
  }
  if(name=="scan_start_failure"){
    scan_ok=false;assert(!start(true));stopped();return;
  }
  if(name=="scan_timeout"||name=="scan_fault"){
    scanning();radio.state=name=="scan_timeout"?WifiSetupRadioState::Failed:WifiSetupRadioState::Fault;
    poll();stopped();has("scan failed or timed out");return;
  }
  if(name=="absent"){
    scanning();row(0,"Example unrelated AP");radio.state=WifiSetupRadioState::Idle;poll();
    stopped();has("not visible on 2.4 GHz");lacks("Example unrelated AP");return;
  }
  if(name.rfind("auth_",0)==0){
    const int auth=std::stoi(name.substr(5));scanning();row(auth);radio.state=WifiSetupRadioState::Idle;poll();
    stopped();has("Authentication required");has(("auth="+std::to_string(auth)+" ").c_str());return;
  }
  if(name=="conflicting"){
    scanning();row();row(5,"Guest-Net",1);radio.state=WifiSetupRadioState::Idle;poll();
    stopped();has("conflicting security");return;
  }
  if(name=="truncated"){
    scanning();row();radio.truncated=true;radio.state=WifiSetupRadioState::Idle;poll();
    stopped();has("scan truncated");return;
  }
  if(name=="connect_start_failure"){
    scanning();row();connect_ok=false;radio.state=WifiSetupRadioState::Idle;poll();
    assert(connect_calls==1&&!busy());has("Cannot start open connection");return;
  }
  if(name=="two_attempt_limit"){
    scanning();row();radio.state=WifiSetupRadioState::Idle;poll();
    radio.state=WifiSetupRadioState::Failed;radio.error=pro::wifi_setup::Error::Dhcp;
    poll();assert(connect_calls==2&&busy());radio.state=WifiSetupRadioState::Failed;
    radio.step=WifiSetupRadioStep::Connect;radio.sdk_error=-123;radio.disconnect_reason=201;poll();
    assert(connect_calls==2&&!busy());poll();assert(connect_calls==2);
    has("after two attempts");has("sdk_error=-123");has("disconnect_reason=201");return;
  }
  if(name=="connect_fault"){
    scanning();row();radio.state=WifiSetupRadioState::Idle;poll();
    radio.state=WifiSetupRadioState::Fault;poll();assert(connect_calls==1&&!busy());has("Radio fault");return;
  }
  if(name=="missing_netif"||name=="lease_read_failure"||name=="probe_allocation_failure"){
    scanning();row();radio.state=WifiSetupRadioState::Idle;poll();
    if(name=="missing_netif")WiFi.STA.present=false;
    if(name=="lease_read_failure")lease_error=-9;
    if(name=="probe_allocation_failure")probe_start_ok=false;
    radio.state=WifiSetupRadioState::LocalConnected;poll();assert(!busy());has("phase=failed");return;
  }
  if(name=="cancel_scan"){
    scanning();cancel();assert(!busy()&&disconnect_calls==2);has("phase=cancelled");return;
  }
  connected();
  if(name=="cancel_probe"){
    goodResult();
    cancel();assert(probe_cancels==1&&busy()&&disconnect_calls==1);
    poll();assert(busy()&&disconnect_calls==1);cancel();assert(probe_cancels==2);
    probe_ready=true;poll();assert(!busy()&&!probe_busy&&disconnect_calls==2);
    has("phase=cancelled");lacks("internet=available");
    assert(std::strstr(screen(),"Internet Available")==nullptr);return;
  }
  if(name=="identity_ssid"||name=="identity_ip"||name=="identity_state"){
    goodResult();if(name=="identity_ssid")WiFi.name="Other network";
    if(name=="identity_ip")WiFi.address=IPAddress(0x0803020a);
    if(name=="identity_state")radio.state=WifiSetupRadioState::Failed;
    finishProbe();has("phase=failed");has("results are not valid");
    // A rejected result must not simultaneously report an available internet
    // connection or a green LCD result for the old identity.
    lacks("internet=available");assert(std::strstr(screen(),"Internet Available")==nullptr);return;
  }
  if(name=="success"||name=="restart_clears_result"||name=="cancel_complete"||name.rfind("later_identity_",0)==0){
    goodResult();finishProbe();has("phase=complete");has("internet=available");has("captive_portal=not detected");
    has("ip=10.2.3.7 gateway=10.2.3.1 dns_server=10.2.3.1");has("dns_http=ok dns_https=ok");has("uploads=disabled");
    has("Internet available; no files uploaded");
    assert(std::strstr(screen(),"Internet Available"));
    if(name=="restart_clears_result"){
      USBSerial.output.clear();assert(start(true));has("internet=unverified");lacks("internet=available");
    }else if(name=="cancel_complete"){
      const auto previous_screen=std::string(screen());const int before=disconnect_calls;
      USBSerial.output.clear();cancel();assert(!busy()&&disconnect_calls==before&&!probe_cancels);
      assert(screen()==previous_screen);has("no active diagnostic; setup unchanged");
      assert(radio.state==WifiSetupRadioState::LocalConnected);
    }else if(name.rfind("later_identity_",0)==0){
      if(name=="later_identity_ssid")WiFi.name="Other network";
      if(name=="later_identity_ip")WiFi.address=IPAddress(0x0803020a);
      if(name=="later_identity_state"){radio.state=WifiSetupRadioState::Failed;WiFi.connection_status=WL_DISCONNECTED;}
      poll();assert(!busy());assert(std::strstr(screen(),"Last test is old"));
      assert(std::strstr(screen(),"Internet Available")==nullptr);
      USBSerial.output.clear();report();lacks("internet=available");
      lacks("Internet available");has("internet=unverified");has("historical");
    }
    return;
  }
  if(name=="dns_fallback"){
    // Lease capture already ran; a separate restart exercises fallback DNS.
    cancel();probe_ready=true;poll();WiFi.dns[0]=IPAddress();USBSerial.output.clear();
    assert(start(true));row();radio.state=WifiSetupRadioState::Idle;poll();
    radio.state=WifiSetupRadioState::LocalConnected;poll();has("dns_server=8.8.8.8");return;
  }
  if(name=="dns_failure"){
    probe_result.clock_valid=true;finishProbe();has("dns_http=not verified dns_https=not verified");
    has("internet=unverified");has("Internet not verified");return;
  }
  if(name=="clock_failure"){
    probe_result.dns_http=true;probe_result.dns_https=true;probe_result.http_status=204;
    finishProbe();has("HTTPS not attempted: clock not synchronized");has("internet=unverified");return;
  }
  goodResult();
  if(name=="http_redirect")probe_result.http_status=302;
  else if(name=="http_intercept")probe_result.http_status=200;
  else if(name=="https_redirect")probe_result.https_status=302;
  else if(name=="tls_failure"){
    probe_result.https_verified=false;probe_result.https_status=0;
    probe_result.https_error=-11;probe_result.tls_error=-0x2700;
  }else assert(!"unknown scenario");
  finishProbe();has("internet=unverified");
  if(name=="tls_failure"){has("captive_portal=unknown");has("tls_verified=no");has("tls_error=-9984");}
  else {has("captive_portal=possible");has("Possible captive portal");assert(std::strstr(screen(),"Login may be required"));}
}
}

bool wifi_setup_radio_disconnect(){++disconnect_calls;if(disconnect_ok){radio.state=WifiSetupRadioState::Idle;WiFi.connection_status=WL_DISCONNECTED;}return disconnect_ok;}
bool wifi_setup_radio_scan(const char* target){++scan_calls;assert(!std::strcmp(target,"Guest-Net"));
  if(scan_ok)radio.state=WifiSetupRadioState::Scanning;
  return scan_ok;
}
bool wifi_setup_radio_connect(const pro::wifi_setup::CheckProfile& profile){
  ++connect_calls;assert(profile.id==0&&profile.ssid=="Guest-Net"&&profile.secret.empty());
  assert(profile.security==pro::wifi_setup::Security::Open);
  if(connect_ok)radio.state=WifiSetupRadioState::Connecting;
  return connect_ok;
}
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot(){return radio;}
int esp_netif_get_ip_info(void*,esp_netif_ip_info_t* lease){
  *lease={{0x0703020a},{0x00ffffff},{0x0103020a}};return lease_error;
}
namespace pro::guest_diag {
bool internetStart(){++probe_starts;if(probe_start_ok)probe_busy=true;return probe_start_ok;}
void internetCancel(){++probe_cancels;}
bool internetBusy(){return probe_busy;}
bool internetReap(InternetResult& out){++reap_calls;if(!probe_busy||!probe_ready)return false;
  out=probe_result;probe_busy=false;return true;}
}
int main(int argc,char** argv){assert(argc==2);scenario(argv[1]);std::cout<<"PASS "<<argv[1]<<"\n";}
