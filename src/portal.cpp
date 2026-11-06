#include "portal.h"
#include "config_loader.h"
#include "pro_device.h"
#include "pro_http.h"
#include "pro_journal.h"
#include "usb_storage.h"
#include "portal_ui.h"
#include "koofr_ca.h"
#include "wifi_scan.h"
#include "setup_identity.h"
#include "setup_access.h"
#include "guest_wifi.h"
#include "setup_network.h"
#include "lcd.h"
#include "diagnostics.h"
#include "ble_feature.h"
#include <esp_wifi.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <base64.h>
#include <esp_heap_caps.h>
#include <memory>
static std::unique_ptr<WiFiServer> server,monitor;
static IPAddress monitorAddress;
static std::string historyIdentity;
static String password,ssid,csrf,authorization;
static uint32_t started=0,rebootAt=0,resumeAt=0,blockedUntil=0;
static uint8_t failedAuth=0;
static bool expired=false;
static String randomHex(unsigned bytes){String s;s.reserve(bytes*2);const char* h="0123456789abcdef";for(unsigned i=0;i<bytes;++i){uint8_t b=esp_random();s+=h[b>>4];s+=h[b&15];}return s;}
static void response(WiFiClient& client,int code,const char* type,const String& body,bool download=false) {
  const char* reason=code==200?"OK":code==204?"No Content":code==401?"Unauthorized":code==403?"Forbidden":code==409?"Conflict":code==429?"Too Many Requests":"Bad Request";
  client.printf("HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Length: %u\r\nContent-Type: %s\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nContent-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n",code,reason,body.length(),type);
  if(code==401)client.print("WWW-Authenticate: Basic realm=\"Cloud Stick Pro\"\r\n");
  if(download)client.print("Content-Disposition: attachment; filename=\"cloud-stick-diagnostics.json\"\r\n");
  client.print("\r\n");client.print(body);
}
static const char* ownerName(pro::Owner o){switch(o){case pro::Owner::Host:return backup_policy().background?"USB available; automatic backups":"USB available; eject for manual backup";case pro::Owner::Ejected:return "USB safely ejected";case pro::Owner::Local:return "USB ejected; read-only backup";default:return "Fault; inspect SD on computer, reboot";}}
static String statusJSON(bool dashboard=false) {
  auto s=backup_status();JsonDocument d;
  d["current_file"]=s.currentFile; // Authenticated dashboard only; diagnostic exports omit filenames.
  d["dashboard_only"]=dashboard;d["watch_folder"]=backup_folder();d["dashboard_url"]=String("http://")+WiFi.localIP().toString()+"/";d["firmware"]=FirmwareVersion;d["device"]=maintenance_active()?"Maintenance; backups paused":"Automatic backups running";d["wifi"]=guest_wifi_active()?"Protected setup AP; guest connection enabled":"Protected setup AP; station paused";d["cloud"]=s.cloud?"Last contact verified":"Unknown or failed";d["storage"]=ownerName(storage_owner());
  d["setup_ip"]=WiFi.softAPIP().toString();d["station_ip"]=WiFi.localIP().toString();d["station_mask"]=WiFi.subnetMask().toString();
  if(s.capacityKnown){d["capacity_bytes"]=s.total;d["free_bytes"]=s.free;}else{d["capacity_bytes"]=nullptr;d["free_bytes"]=nullptr;}
  d["has_scan_since_boot"]=s.hasScan;d["scan_complete"]=s.scanComplete;
  if(s.hasScan){d["pending"]=s.pending;d["verified"]=s.verified;d["failed"]=s.failed;d["transferred_unverified"]=s.transferred;d["skipped"]=s.skipped;}else{d["pending"]=nullptr;d["verified"]=nullptr;d["failed"]=nullptr;d["transferred_unverified"]=nullptr;d["skipped"]=nullptr;}
  d["last_backup_unix"]=s.lastBackup;d["scanning"]=s.scanning;d["message"]=s.message;d["error_code"]=s.error;d["current_heap"]=ESP.getFreeHeap();d["minimum_heap"]=ESP.getMinFreeHeap();d["largest_free_block"]=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);d["backup_stack_free"]=s.stackFree;d["csrf"]=csrf;
  diagnostics_add_status(d);
  String body;serializeJson(d,body);return body;
}
static void accessIdentity(){
  csrf=randomHex(16);
  authorization="Basic "+base64::encode(String(setup_access::Username)+":"+setup_access::WebPassword);
  auto c=load_config();historyIdentity=hash_text(std::string(c.server_url.c_str())+"\n"+c.username.c_str());
}
static void flashResponse(WiFiClient& client,const char* type,const char* data){
  size_t size=strlen_P(data);
  client.printf("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %u\r\nContent-Type: %s\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nContent-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n\r\n",unsigned(size),type);
  // ESP32 flash is memory mapped. Send bounded pieces, never copy the page into RAM.
  size_t sent=0;uint32_t start=millis();
  while(sent<size&&client.connected()&&millis()-start<5000){size_t n=client.write(reinterpret_cast<const uint8_t*>(data+sent),std::min<size_t>(512,size-sent));if(!n)break;sent+=n;delay(1);}
}
bool portal_start() {
#if CSP_ENABLE_BLE_MANAGEMENT
  // No shared-default AP configuration bypass exists in the synthetic BLE target.
  return false;
#else
  if(server)return true;
  set_maintenance(true);if(backup_status().active)return false;
  WiFi.persistent(false);WiFi.disconnect(false);WiFi.mode(WIFI_AP);
  if(!load_setup_identity(ssid,password)||!setup_network_load())return false;
  // Keep the saved unique network name; use the owner's fixed setup passwords.
  password=setup_access::WifiPassword;
  if(password!=setup_access::WifiPassword)return false;
  if(monitor){monitor->end();monitor.reset();}
  accessIdentity();
  // Advertise the AP's bounded DNS relay before the phone obtains its lease.
  IPAddress ap=setup_network_ip(),lease=ap;lease[3]=2;
  if(!WiFi.softAPConfig(ap,ap,IPAddress(255,255,255,0),lease,ap))return false;
  if(!WiFi.softAP(ssid.c_str(),password.c_str(),1,false,1))return false;
  server.reset(new(std::nothrow) WiFiServer(WiFi.softAPIP(),80));if(!server){WiFi.softAPdisconnect(true);return false;}
  server->begin();
  CloudConfig saved=load_config();
  if(saved.is_valid){String error;guest_wifi_connect(saved.ssid,saved.wifi_open?String():saved.wifi_password,error);request_connection_check();}
  allow_connection_checks(true);started=millis();expired=false;return true;
#endif
}
static void handle(WiFiClient& client,bool dashboard=false) {
  client.setTimeout(2000);
  IPAddress peer=client.remoteIP();
  if(!dashboard&&(client.localIP()!=WiFi.softAPIP()||!setup_network_client(peer))){response(client,403,"text/plain","AP only");return;}
  uint32_t begin=millis();std::string header;header.reserve(pro::HeaderLimit);
  while(header.size()<pro::HeaderLimit && millis()-begin<5000) {
    int c=client.read();if(c<0){if(!client.connected())return;delay(1);continue;}
    header+=char(c);
    if(header.size()>=4&&header.compare(header.size()-4,4,"\r\n\r\n")==0)break;
  }
  pro::Request request;const char* parseError=nullptr;if(!pro::parseRequest(header,request,&parseError,client.localIP().toString().c_str())){response(client,400,"text/plain",parseError?parseError:"Invalid request");return;}
  if(blockedUntil&&int32_t(millis()-blockedUntil)<0){response(client,429,"text/plain","Wait before retrying login");return;}
  if(!pro::constantEqual(request.authorization,authorization.c_str())) {
    if(++failedAuth>=5){blockedUntil=millis()+30000;failedAuth=0;}
    response(client,401,"text/plain","Use admin and the web password shown on the device");return;
  }
  failedAuth=0;
#if CSP_ENABLE_BLE_MANAGEMENT
  if(!pro::dashboardRoute(request.method,request.path)){response(client,403,"text/plain","Milestone 1 is read-only; configuration is disabled.");return;}
#endif
  if(!dashboard)started=millis(); // Authenticated use keeps setup open; passwords never rotate.
  if(dashboard&&!pro::dashboardRoute(request.method,request.path)){response(client,403,"text/plain","This dashboard is read-only. Hold the dongle button for 3 seconds to change setup.");return;}
  if(request.method=="GET") {
    if(request.path=="/")flashResponse(client,"text/html; charset=utf-8",Page);
    else if(request.path=="/app.js")flashResponse(client,"application/javascript",Script);
    else if(request.path=="/status")response(client,200,"application/json",statusJSON(dashboard));
    else if(request.path=="/diagnostics")response(client,200,"application/json",diagnostics_json(true),true);
    else if(request.path=="/wifi-scan")response(client,200,"application/json",wifi_scan_json());
    else if(request.path=="/wifi-connect"){response(client,200,"application/json",guest_wifi_json());guest_wifi_relocation_sent();}
    else if(request.path=="/favicon.ico")response(client,204,"image/x-icon","");
    else if(request.path=="/presets"){JsonDocument d;d["koofr"]["server_url"]=KoofrServer;d["koofr"]["ca_pem"]=KoofrCA;auto c=load_config();d["saved"]["watch_folder"]=c.watch_folder;d["saved"]["backup_mode"]=c.backup_mode;d["saved"]["idle_seconds"]=c.idle_seconds;d["saved"]["timezone"]=c.timezone;d["saved"]["window_start"]=c.window_start;d["saved"]["window_end"]=c.window_end;d["saved"]["ntp_server"]=c.ntp_server;d["saved"]["sync_interval_s"]=c.sync_interval_s;d["saved"]["max_file_size_mb"]=c.max_file_size_mb;String body;serializeJson(d,body);response(client,200,"application/json",body);}
    else response(client,400,"text/plain","Method not supported");
    return;
  }
  if(!pro::constantEqual(request.csrf,csrf.c_str())){response(client,403,"text/plain","Invalid request token");return;}
  if((request.path=="/wifi-connect"||request.path=="/wifi-disconnect")&&request.length>512){response(client,400,"text/plain","Wi-Fi request is too large");return;}
  String body;body.reserve(request.length);
  while(body.length()<request.length && millis()-begin<5000) {
    int c=client.read();if(c<0){if(!client.connected())return;delay(1);continue;}body+=char(c);
  }
  if(body.length()!=request.length){response(client,400,"text/plain","Incomplete request");return;}
  if(request.path=="/history"){
    JsonDocument input;if(request.length>96||deserializeJson(input,body)||!input["cursor"].is<const char*>()){response(client,400,"text/plain","Invalid history cursor");return;}
    std::string cursor=input["cursor"].as<const char*>();
    if(!cursor.empty()&&(cursor.size()!=52||cursor.find_first_not_of("abcdefghijklmnopqrstuvwxyz234567")!=std::string::npos)){response(client,400,"text/plain","Invalid history cursor");return;}
    input.clear();JsonDocument output;auto rows=output["files"].to<JsonArray>();pro::FileJournal journal("/littlefs/queue");bool more=true;unsigned examined=0;
    while(rows.size()<4&&examined++<pro::FileLimit){pro::Record record;std::string next;auto result=journal.next(cursor,next,record);
      if(result==pro::Load::Missing){more=false;break;}if(result==pro::Load::IOError){output["unavailable"]=true;more=false;break;}cursor=next;
      if(result!=pro::Load::Found){output["damaged_receipt"]=true;continue;}
      std::string folder=backup_folder().c_str();
      if(record.destination!=historyIdentity||record.path.compare(0,folder.size()+1,folder+"/")!=0)continue;
      auto row=rows.add<JsonObject>();row["path"]=record.path;row["bytes"]=record.size;row["state"]=record.state==pro::State::Verified?"verified":record.state==pro::State::Transferred?"checking":record.attempts?"retry":"pending";
      row["verified_unix"]=record.verifiedAt;row["retry_unix"]=record.next;row["error_code"]=record.error;row["attempts"]=record.attempts;
    }
    output["cursor"]=more?cursor:"";output["more"]=more;String result;serializeJson(output,result);response(client,200,"application/json",result);return;
  }
  if(request.path=="/wifi-scan"){JsonDocument requestData;if(deserializeJson(requestData,body,DeserializationOption::NestingLimit(1))||!requestData.is<JsonObject>()||requestData.size()!=0){response(client,400,"text/plain","Scan request must be an empty JSON object");return;}if(guest_wifi_active()){response(client,409,"text/plain","Disconnect the guest Wi-Fi connection before searching again");return;}wifi_scan_request();response(client,200,"application/json",wifi_scan_json());return;}
  if(request.path=="/wifi-connect") {
    JsonDocument d;if(deserializeJson(d,body,DeserializationOption::NestingLimit(1))||!d.is<JsonObject>()||!d["ssid"].is<const char*>()||!d["wifi_password"].is<const char*>()||!d["wifi_open"].is<bool>()){response(client,400,"text/plain","Invalid Wi-Fi settings");return;}
    String network=d["ssid"].as<String>(),secret=d["wifi_password"].as<String>();bool open=d["wifi_open"];
    if(network==ssid){response(client,400,"text/plain","Choose your home or guest Wi-Fi, rather than the CloudStickPro setup network");return;}
    if((open&&!secret.isEmpty())||(!open&&secret.length()<8)){response(client,400,"text/plain","Enter the Wi-Fi password, or select No password for an open network");return;}
    if(wifi_scan_busy()){response(client,409,"text/plain","Wait for the Wi-Fi search to finish, then connect");return;}
    String error;if(!guest_wifi_connect(network,secret,error)){response(client,400,"text/plain",error);return;}response(client,200,"application/json",guest_wifi_json());guest_wifi_relocation_sent();return;
  }
  if(request.path=="/wifi-disconnect") {
    JsonDocument d;if(deserializeJson(d,body,DeserializationOption::NestingLimit(1))||!d.is<JsonObject>()||d.size()!=0){response(client,400,"text/plain","Disconnect request must be an empty JSON object");return;}
    guest_wifi_stop();response(client,200,"application/json",guest_wifi_json());return;
  }
  if(request.path=="/resume"){if(!load_config().is_valid){response(client,400,"text/plain","Save your settings first");return;}response(client,200,"text/plain","Resuming automatic backups. Leave CLOUDSTICK plugged in; the USB drive stays available.");resumeAt=millis()+1500;return;}
  if(request.path=="/cloud-check"||request.path=="/cloud-test"){
    JsonDocument d;if(deserializeJson(d,body)||!d.is<JsonObject>()||d.size()!=0){response(client,400,"text/plain","Send an empty JSON object");return;}
    bool ok=request_connection_check(request.path=="/cloud-test");
    response(client,ok?200:409,"text/plain",ok?"Connection check started. Watch the result below.":"A check is already pending, or no settings have been saved yet.");return;
  }
  if(request.path!="/config"){response(client,400,"text/plain","Method not supported");return;}
  JsonDocument d;if(deserializeJson(d,body,DeserializationOption::NestingLimit(3))){response(client,400,"text/plain","Invalid JSON");return;}
  CloudConfig c;c.ssid=d["ssid"]|"";c.wifi_open=d["wifi_open"]|false;c.wifi_password=d["wifi_password"]|"";c.server_url=d["server_url"]|"";c.username=d["username"]|"";c.password=d["password"]|"";c.ca_pem=d["ca_pem"]|"";c.ntp_server=d["ntp_server"]|"";c.sync_interval_s=d["sync_interval_s"]|60;c.max_file_size_mb=d["max_file_size_mb"]|64;
  c.watch_folder=d["watch_folder"]|"QA";c.backup_mode=d["backup_mode"]|"idle";c.idle_seconds=d["idle_seconds"]|120;c.timezone=d["timezone"]|"EST5EDT,M3.2.0,M11.1.0";c.window_start=d["window_start"]|1;c.window_end=d["window_end"]|5;
  if(d["reuse_cloud"]|false){
    auto old=load_config();if(!old.is_valid){response(client,400,"text/plain","No saved cloud account is available");return;}
    c.server_url=old.server_url;c.username=old.username;c.password=old.password;c.ca_pem=old.ca_pem;
  }
  if((d["reuse_wifi"]|false)&&!guest_wifi_fill_config(c)){response(client,400,"text/plain","Your Wi-Fi connection changed. Go back to Wi-Fi and connect again");return;}
  String error;if(!save_config(c,error)){response(client,400,"text/plain",error);return;}
  response(client,200,"text/plain","Settings saved. Eject CLOUDSTICK once and leave it plugged in. It will restart and begin backups automatically.");rebootAt=millis()+1500;
}
static void relocateSetup(uint32_t address){
  IPAddress old=WiFi.softAPIP();
  server->end();server.reset();
  IPAddress ap(address>>24,(address>>16)&255,(address>>8)&255,address&255),lease=ap;lease[3]=2;
  bool ok=WiFi.softAPConfig(ap,ap,IPAddress(255,255,255,0),lease,ap);
  if(ok)ok=setup_network_save(address);
  if(!ok){
    ap=old;lease=old;lease[3]=2;
    WiFi.softAPConfig(ap,ap,IPAddress(255,255,255,0),lease,ap);
  }
  server.reset(new(std::nothrow) WiFiServer(ap,80));
  if(server)server->begin();else ok=false;
  guest_wifi_relocated(ok);
  // Force a fresh DHCP lease; STA, SSID, password and USB ownership stay intact.
  if(ok)esp_wifi_deauth_sta(0);
  lcd_show_setup(ssid.c_str(),password.c_str());
}
void portal_poll() {
  if(!server)return;
  guest_wifi_poll();
  wifi_scan_poll();
  if(!expired){WiFiClient client=server->accept();if(client){handle(client);client.stop();}}
  if(!expired){uint32_t address=guest_wifi_relocation_ready();if(address)relocateSetup(address);}
  if(!server)return;
  if(!expired&&millis()-started>600000){expired=true;allow_connection_checks(false);server->end();wifi_scan_cancel();guest_wifi_stop();WiFi.softAPdisconnect(true);}
  if(((resumeAt&&int32_t(millis()-resumeAt)>=0)||(expired&&!rebootAt&&load_config().is_valid))&&!backup_status().active){
    server->end();server.reset();wifi_scan_cancel();guest_wifi_stop();WiFi.softAPdisconnect(true);
    resumeAt=0;expired=false;resume_saved_backups();return;
  }
  bool requested=(rebootAt&&int32_t(millis()-rebootAt)>=0)||expired;
  auto owner=storage_owner();
  // Never reboot an actively host-owned disk as a side effect of phone setup.
  if(requested&&pro::canRestart(owner,backup_status().active))restart_after_eject();
}
bool portal_active(){return bool(server);}
bool portal_expired(){return expired;}
const String& portal_password(){return password;}
const String& portal_ssid(){return ssid;}

void dashboard_poll(){
  if(portal_active()||maintenance_active()||WiFi.status()!=WL_CONNECTED){if(monitor){monitor->end();monitor.reset();}return;}
  IPAddress address=WiFi.localIP();
  if(!monitor||address!=monitorAddress){if(monitor)monitor->end();accessIdentity();monitor.reset(new(std::nothrow) WiFiServer(address,80));if(!monitor)return;monitorAddress=address;monitor->begin();}
  WiFiClient client=monitor->accept();if(client){handle(client,true);client.stop();}
}
