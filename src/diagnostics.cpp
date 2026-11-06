#include "diagnostics.h"
#include "ble_feature.h"
#include "pro_device.h"
#include "usb_storage.h"
#include <WiFi.h>
#include <USB.h>
#include <USBCDC.h>
#include <nvs.h>
#include <esp_system.h>
#include <ctime>
#include <memory>

namespace {
pro::DiagnosticRing events;
pro::DiagnosticLine input;
uint32_t boot=1,lastPoll=0,lastSave=0,lastReply=0;
bool dirty=false,logStorage=true,configured=false;
int prevWifi=-1,prevOwner=-1,prevClock=-1,prevCheck=-1,prevCode=0,prevError=0;
uint64_t prevVerifiedTime=0;
const char* eventName(unsigned kind){
  switch(kind){case 1:return "boot";case 2:return "wifi_connected";case 3:return "wifi_disconnected";
    case 4:return "storage_owner";case 5:return "network_time_ready";case 6:return "cloud_check";
    case 7:return "backup_error";case 8:return "verified_files";default:return "unknown";}
}
void add(unsigned kind,int code){events.add({boot,millis(),time(nullptr)>=1767225600?uint64_t(time(nullptr)):0,uint8_t(kind),code});dirty=true;}
void appendEvents(JsonDocument& doc){
  auto list=doc["events"].to<JsonArray>();
  for(unsigned i=0;i<events.size();++i){auto e=events.at(i);auto o=list.add<JsonObject>();
    o["boot"]=e.boot;o["uptime_ms"]=e.ms;o["unix_time"]=e.time;o["kind"]=e.kind;o["event"]=eventName(e.kind);o["code"]=e.code;
  }
}
void persist(){
  JsonDocument doc;doc["schema"]=1;doc["boot"]=boot;appendEvents(doc);String raw;serializeJson(doc,raw);
  nvs_handle_t h;bool ok=raw.length()<=4096&&nvs_open("csp-diag",NVS_READWRITE,&h)==ESP_OK;
  if(ok){ok=nvs_set_blob(h,"events",raw.c_str(),raw.length())==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);}
  logStorage=ok;dirty=!ok;lastSave=millis();
}
}
void diagnostics_init(bool hasConfig){
  configured=hasConfig;USBSerial.setTxTimeoutMs(100);
  nvs_handle_t h;
  if(nvs_open("csp-diag",NVS_READONLY,&h)==ESP_OK){
    size_t n=0;if(nvs_get_blob(h,"events",nullptr,&n)==ESP_OK&&n>0&&n<=4096){
      std::unique_ptr<char[]> raw(new(std::nothrow) char[n]);
      if(raw&&nvs_get_blob(h,"events",raw.get(),&n)==ESP_OK){
        JsonDocument d;if(!deserializeJson(d,raw.get(),n,DeserializationOption::NestingLimit(4))&&d["schema"]==1&&d["events"].is<JsonArray>()&&d["events"].size()<=16){
          boot=(d["boot"]|uint32_t(0))+1;if(!boot)boot=1;
          for(JsonObject e:d["events"].as<JsonArray>()){
            unsigned kind=e["kind"]|0;if(kind<1||kind>8)continue;
            events.add({e["boot"]|0U,e["uptime_ms"]|0U,e["unix_time"]|uint64_t(0),uint8_t(kind),e["code"]|0});
          }
        }
      }
    }nvs_close(h);
  }
  add(1,int(esp_reset_reason()));persist();
}
void diagnostics_add_status(JsonDocument& d){
  auto policy=backup_policy();
  d["backup_mode"]=!policy.background?"eject":policy.overnight?"overnight":"idle";d["idle_seconds"]=policy.idleSeconds;d["quiet_remaining_seconds"]=storage_quiet_remaining(policy.idleSeconds);d["backup_window_open"]=backup_window_open();d["window_start"]=policy.startHour;d["window_end"]=policy.endHour;d["usb_write_generation"]=storage_generation();
  auto s=backup_status();auto c=connection_check();bool wifi=WiFi.status()==WL_CONNECTED;
  d["firmware"]=FirmwareVersion;d["configured"]=configured;d["uptime_ms"]=millis();d["boot"]=boot;
  d["wifi_connected"]=wifi;d["wifi_status"]=int(WiFi.status());d["wifi_signal_dbm"]=wifi?WiFi.RSSI():0;
  d["station_ip"]=WiFi.localIP().toString();d["gateway_ip"]=WiFi.gatewayIP().toString();d["dns_ip"]=WiFi.dnsIP().toString();
  d["clock_ready"]=time(nullptr)>=1767225600;d["maintenance"]=maintenance_active();
  d["storage_owner"]=int(storage_owner());d["sd_ready"]=storage_ready();d["backup_active"]=s.active;
  d["has_scan_since_boot"]=s.hasScan;d["scan_complete"]=s.scanComplete;d["pending"]=s.pending;d["verified"]=s.verified;d["failed"]=s.failed;d["skipped"]=s.skipped;
  d["transfer_phase"]=int(s.phase);d["transfer_done_bytes"]=s.transferDone;d["transfer_total_bytes"]=s.transferTotal;d["transfer_bytes_per_second"]=s.transferSpeed;d["progress_percent"]=s.progress;
  d["last_backup_unix"]=s.lastBackup;d["error_code"]=s.error;d["error_help"]=pro::errorHelp(s.error);d["message"]=s.message;
  d["cloud_check"]=pro::checkName(c.state);d["cloud_code"]=c.code;d["cloud_checked_unix"]=c.checkedAt;
  d["cloud_help"]=c.state==pro::CheckState::Ready?"Cloud login and folder access passed. Automatic backups can use this connection.":c.state==pro::CheckState::Verified?"Test file uploaded and its contents verified by download.":c.state==pro::CheckState::Checking?"Checking the saved cloud connection...":pro::errorHelp(c.code);
  d["cloud_test_file"]=c.testFile;d["free_heap"]=ESP.getFreeHeap();d["minimum_heap"]=ESP.getMinFreeHeap();
  d["diagnostic_storage_ok"]=logStorage;
}
String diagnostics_json(bool log){JsonDocument d;diagnostics_add_status(d);if(log)appendEvents(d);String output;serializeJson(d,output);return output;}
void diagnostics_poll(){
  // USB commands carry no credentials and never eject or modify the SD card.
  for(unsigned n=0;n<64&&USBSerial.available();++n){
    auto command=input.push(char(USBSerial.read()));if(command==pro::DiagnosticCommand::None)continue;
    if(lastReply&&millis()-lastReply<200)continue;lastReply=millis();
    if(command==pro::DiagnosticCommand::Check||command==pro::DiagnosticCommand::Test){
      bool ok=request_connection_check(command==pro::DiagnosticCommand::Test);
      USBSerial.println(ok?"{\"accepted\":true}":"{\"accepted\":false,\"reason\":\"Check already pending or no saved configuration\"}");
    }else if(command==pro::DiagnosticCommand::Status||command==pro::DiagnosticCommand::Log)USBSerial.println(diagnostics_json(command==pro::DiagnosticCommand::Log));
    else if(command==pro::DiagnosticCommand::Setup){
#if CSP_ENABLE_BLE_MANAGEMENT
      USBSerial.println("{\"setup_requested\":false,\"reason\":\"Milestone 1 requires the physical BOOT button\"}");
#else
      request_setup();USBSerial.println("{\"setup_requested\":true}");
#endif
    }
    else USBSerial.println("{\"error\":\"Use STATUS, LOG, CHECK, TEST or SETUP\"}");
  }
  if(millis()-lastPoll<1000)return;lastPoll=millis();
  auto s=backup_status();auto c=connection_check();int wifi=WiFi.status()==WL_CONNECTED,owner=int(storage_owner()),clock=time(nullptr)>=1767225600;
  if(wifi!=prevWifi){add(wifi?2:3,int(WiFi.status()));prevWifi=wifi;}
  if(owner!=prevOwner){add(4,owner);prevOwner=owner;}
  if(clock&&prevClock!=1)add(5,0);prevClock=clock;
  if(int(c.state)!=prevCheck||c.code!=prevCode){add(6,c.code);prevCheck=int(c.state);prevCode=c.code;}
  if(s.error!=prevError){add(7,s.error);prevError=s.error;}
  if(s.lastBackup!=prevVerifiedTime&&s.lastBackup)add(8,s.verified);prevVerifiedTime=s.lastBackup;
  // Bound flash writes even when a network repeatedly disconnects.
  if(dirty&&millis()-lastSave>=30000)persist();
}
