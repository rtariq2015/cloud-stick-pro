#include "ble_feature.h"
#if CSP_ENABLE_WIFI_SETUP
#include "wifi_setup_runtime.h"
void setup(){wifi_setup_runtime_setup();}
void loop(){wifi_setup_runtime_loop();}
#else
#include "esp_arduino_version.h"
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,3,0)
#error ESP32 Arduino Core 3.3.0 or later is required
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <USB.h>
#include <USBCDC.h>
#include <esp_system.h>
#include "config_loader.h"
#include "usb_storage.h"
#include "lcd.h"
#include "pro_device.h"
#include "portal.h"
#include "diagnostics.h"
#include "ble_feature.h"
#if CSP_ENABLE_BLE_MANAGEMENT
#include "ble_management.h"
#include "ble_platform_prepare.h"
#include "ble_memory_status.h"
#include "ble_alloc_trace.h"
#include "pro_ble_lifecycle.h"
#include <esp_heap_caps.h>
#endif
#if !CSP_BLE_ISOLATED_TEST
RTC_DATA_ATTR static uint32_t backupBootCookie=0;
static CloudConfig config;
#endif
static bool maintenanceRequested=false;
static uint32_t buttonDown=0,lastDraw=0,lastReconnect=0;
void request_setup(){
#if CSP_ENABLE_BLE_MANAGEMENT
  // USB diagnostics and legacy HTTP cannot open a physical BLE ownership window.
  backup_message("M1: hold BOOT for BLE setup");
#else
  maintenanceRequested=true;set_maintenance(true);
#endif
}
void resume_saved_backups(){
#if !CSP_BLE_ISOLATED_TEST
  if(!config.is_valid)return;
  maintenanceRequested=false;set_maintenance(false);
  WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);WiFi.setMinSecurity(config.wifi_open?WIFI_AUTH_OPEN:WIFI_AUTH_WPA2_PSK);
  WiFi.begin(config.ssid.c_str(),config.wifi_open?nullptr:config.wifi_password.c_str());
#endif
}
void restart_after_eject(){
#if CSP_ENABLE_BLE_MANAGEMENT
  // The read-only prototype has no reset/configuration command, even after eject.
  return;
#else
  if(!pro::canRestart(storage_owner(),backup_status().active))return;
  auto owner=storage_owner();auto saved=load_config();backupBootCookie=saved.backup_mode=="eject"&&(owner==pro::Owner::Local||owner==pro::Owner::Ejected)?0x43535042:0;
  ESP.restart();
#endif
}
#if CSP_ENABLE_BLE_MANAGEMENT
static pro::BleLifecycle bleLifecycle;
static bool bleStartAttempted=false;
static uint32_t bleBeforeFree=0,bleBeforeLargest=0;
static uint32_t bleAfterFree=0,bleAfterLargest=0;
static bool bleRecoveryChecked=false,bleStopConfirmed=false,bleMemoryRestored=false;
BleMemoryStatus ble_memory_status(){
  return {bleBeforeFree,bleBeforeLargest,bleAfterFree,bleAfterLargest,
    bleRecoveryChecked,bleStopConfirmed,bleMemoryRestored};
}
static uint32_t internalFree(){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
static uint32_t internalLargest(){return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
static void finishBle(){
  bool stopped=ble_management_stop();
  bleAfterFree=internalFree();bleAfterLargest=internalLargest();
  bool restored=!bleStartAttempted||pro::BleLifecycle::restored(bleBeforeFree,bleBeforeLargest,bleAfterFree,bleAfterLargest);
  bleRecoveryChecked=true;bleStopConfirmed=stopped;bleMemoryRestored=restored;
#if CSP_BLE_ALLOC_TRACE
  ble_alloc_trace_stop(millis());
#endif
  if(!bleLifecycle.stopped(stopped,restored)){
    // Fail closed: never reclaim live BLE buffers, resume TLS or reset host USB.
    backup_message("M1 memory/stop gate; backups paused",-2402);return;
  }
  bleStartAttempted=false;
  set_maintenance(false);
  backup_message("M1 BLE closed; isolated USB test");
}
static void pollBle(){
  if(!ble_platform_ready())return;
  auto action=bleLifecycle.button(digitalRead(0)==LOW,millis());
  if(action==pro::BleLifecycle::Action::Pause){
    set_maintenance(true);backup_message("M1 BLE: waiting for backup to stop");
  }
  ble_management_poll();
#if CSP_BLE_ALLOC_TRACE
  ble_alloc_trace_poll(millis());
  if(ble_management_authenticated())ble_alloc_trace_phase(pro::trace::Phase::Authenticated);
#endif
  action=bleLifecycle.poll(millis(),backup_status().active,ble_management_active()&&
    (!pro::setup_session::Guided||!ble_management_status().teardown_failed),internalFree(),internalLargest());
  if(action==pro::BleLifecycle::Action::Start){
    // The isolated image never starts Wi-Fi, TLS, portals or backup workers.
    // Keep the existing independent TinyUSB callbacks running throughout.
    bleBeforeFree=internalFree();bleBeforeLargest=internalLargest();
    bleAfterFree=bleAfterLargest=0;
    bleRecoveryChecked=bleStopConfirmed=bleMemoryRestored=false;
    bool ok=pro::BleLifecycle::canStart(bleBeforeFree,bleBeforeLargest);
#if CSP_BLE_ALLOC_TRACE
    if(ok)ble_alloc_trace_begin();
#endif
    if(ok){bleStartAttempted=true;ble_management_set_connection_status(false);ok=ble_management_begin();}
#if CSP_BLE_ALLOC_TRACE
    if(ok)ble_alloc_trace_phase(pro::trace::Phase::Ready);
#endif
    bleLifecycle.started(ok);
    if(!ok){backup_message("M1 BLE could not start",-2401);finishBle();}
    else backup_message("M1 BLE synthetic read-only session");
  }else if(action==pro::BleLifecycle::Action::Stop)finishBle();
}
#endif
void setup() {
  delay(500);pinMode(0,INPUT_PULLUP);lcd_init();lcd_show_loading();
#if CSP_ENABLE_BLE_MANAGEMENT
  // Boot-time only: the USB drive is not yet enumerated and no worker runs.
  // Failures leave USB diagnostics alive but never admit a BLE window.
  const bool prepared=ble_platform_prepare();
  (void)ble_management_status(); // initialize the bounded state mutex before baseline
#endif
#if CSP_BLE_ISOLATED_TEST
  // Never load saved credentials, mount a receipt journal or persist diagnostics.
  // USB remains the original MSC implementation; only the host may write media.
  init_usb_storage(false);diagnostics_init(false);
#else
  bool backupBoot=pro::resumeBackupBoot(esp_reset_reason()==ESP_RST_SW,backupBootCookie);backupBootCookie=0;
  init_usb_storage(backupBoot);config=load_config();diagnostics_init(config.is_valid);
  if(config.is_valid) {
    WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
    WiFi.setMinSecurity(config.wifi_open?WIFI_AUTH_OPEN:WIFI_AUTH_WPA2_PSK);
    WiFi.begin(config.ssid.c_str(),config.wifi_open?nullptr:config.wifi_password.c_str());
    configTzTime(config.timezone.c_str(),config.ntp_server.c_str());
    start_backup(config);
  } else {
#if !CSP_ENABLE_BLE_MANAGEMENT
    maintenanceRequested=true;set_maintenance(true);
#endif
  }
#endif
#if CSP_ENABLE_BLE_MANAGEMENT
  if(!prepared)backup_message("BLE platform self-test failed",ble_platform_error());
#endif
  // Diagnostic excludes SSIDs, URLs and passwords.
  if(USBSerial)USBSerial.printf("Cloud Stick Pro %s reset reason=%d\n",FirmwareVersion,int(esp_reset_reason()));
}
void loop() {
  poll_storage();
  diagnostics_poll();
#if CSP_ENABLE_BLE_MANAGEMENT
  if(!ble_platform_ready()){
    if(millis()-lastDraw>=500){lastDraw=millis();lcd_show_text("BLE self-test failed");}
    delay(5);return;
  }
  pollBle();
  if(bleLifecycle.paused()){
    if(millis()-lastDraw>=500){lastDraw=millis();lcd_show_text(bleLifecycle.state()==pro::BleLifecycle::State::Blocked?"BLE needs review":ble_management_authenticated()?"BLE authenticated":"BLE M1 setup");}
    delay(5);return;
  }
#else
  dashboard_poll();
  if(digitalRead(0)==LOW) {
    if(!buttonDown)buttonDown=millis();
    uint32_t held=millis()-buttonDown;
    if(held>=3000&&!maintenanceRequested){maintenanceRequested=true;set_maintenance(true);}
    // Reset requires a continuous physical hold, never an unauthenticated endpoint.
    auto owner=storage_owner();
    if(held>=10000&&pro::canRestart(owner,backup_status().active)){reset_config();ESP.restart();}
  } else buttonDown=0;
  if(maintenanceRequested&&!portal_active()&&!backup_status().active) {
    if(portal_start())lcd_show_setup(portal_ssid().c_str(),portal_password().c_str());
    else backup_message("Cannot start setup access point",-2201);
  }
  if(portal_active()) {
    portal_poll();static bool shownExpired=false;
    if(portal_expired()&&!shownExpired){shownExpired=true;lcd_show_text("Eject to restart");}
    delay(2);return;
  }
#endif
#if CSP_BLE_ISOLATED_TEST
  if(millis()-lastDraw>=1000){lastDraw=millis();lcd_show_text("M1: hold BOOT 3s");}
#else
  if(config.is_valid && WiFi.status()!=WL_CONNECTED && millis()-lastReconnect>30000){lastReconnect=millis();WiFi.reconnect();}
  if(millis()-lastDraw>=500){lastDraw=millis();lcd_pro_update(backup_status(),storage_owner(),WiFi.status()==WL_CONNECTED);}
#endif
  delay(5);
}
#endif // CSP_ENABLE_WIFI_SETUP
