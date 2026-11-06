#include "ble_feature.h"
#if CSP_BLE_ISOLATED_TEST
// This runtime replaces all production configuration/network/backup/receipt
// workers at link time. It contains only volatile status and read-only telemetry.
#include "ble_management.h"
#include "ble_platform_prepare.h"
#include "ble_memory_status.h"
#include "ble_alloc_trace.h"
#include "ble_sdk_lifecycle.h"
#include "ble_isolated_flash_guard.h"
#include "diagnostics.h"
#include "pro_device.h"
#include "usb_storage.h"
#include <USB.h>
#include <USBCDC.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <atomic>

namespace {
BackupStatus status;
bool paused=false;
pro::DiagnosticLine input;
pro::DiagnosticRing events;
uint32_t lastReply=0;
std::atomic<uint32_t> rejectedFlashWrites{0};
}
// Arduino's startup can erase NVS on an initialization error; radio calibration
// and SDK stores can also request writes. Fail those requests in this image.
// Never call a __real mutation. This is not enabled in production firmware.
extern "C" esp_err_t __wrap_esp_partition_write(const esp_partition_t*,size_t,const void*,size_t){
  CSP_TRACE_DENIAL(1);
  rejectedFlashWrites.fetch_add(1);return ESP_ERR_NOT_SUPPORTED;
}
extern "C" esp_err_t __wrap_esp_partition_write_raw(const esp_partition_t*,size_t,const void*,size_t){
  CSP_TRACE_DENIAL(2);
  rejectedFlashWrites.fetch_add(1);return ESP_ERR_NOT_SUPPORTED;
}
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t*,size_t,size_t){
  CSP_TRACE_DENIAL(3);
  rejectedFlashWrites.fetch_add(1);return ESP_ERR_NOT_SUPPORTED;
}
BackupStatus backup_status(){return status;}
void backup_message(const char* text,int error){
  snprintf(status.message,sizeof status.message,"%s",text);status.error=error;
  events.add({1,millis(),0,7,error});
}
void set_maintenance(bool enabled){paused=enabled;}
bool maintenance_active(){return paused;}

void diagnostics_init(bool){
  USBSerial.setTxTimeoutMs(100);
  events.add({1,millis(),0,1,int(esp_reset_reason())});
  backup_message("Isolated BLE test; hold BOOT 3 seconds");
}
void diagnostics_add_status(JsonDocument& d){
  const auto b=ble_management_status();
  d["firmware"]=FirmwareVersion;d["isolated_test"]=true;
  d["synthetic_credentials_only"]=true;d["diagnostics_persistent"]=false;
  d["uptime_ms"]=millis();d["reset_reason"]=int(esp_reset_reason());
  d["wifi_started"]=false;d["backup_worker_started"]=false;
  d["receipt_worker_started"]=false;d["configured"]=false;
  d["rejected_flash_writes"]=rejectedFlashWrites.load();
  d["rejected_raw_flash_writes"]=ble_isolated_raw_flash_denials();
  d["storage_owner"]=int(storage_owner());d["usb_write_generation"]=storage_generation();
  d["internal_free_bytes"]=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  d["internal_minimum_bytes"]=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  d["internal_largest_bytes"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  d["loop_stack_free_bytes"]=uxTaskGetStackHighWaterMark(nullptr);
  // Self-sampled by the host immediately before its exit acknowledgment.
  // Zero means not measured; never query a deleted task handle from this task.
  d["ble_host_stack_min_bytes_after_exit"]=csp_ble_sdk_host_stack_min_bytes();
  d["ble_active"]=b.active;d["ble_ready"]=csp_ble_sdk_ready();
  d["ble_radio_idle"]=csp_ble_sdk_idle();d["ble_connected"]=b.connected;
  d["ble_authenticated"]=b.authenticated;d["ble_terminal_denial"]=b.terminal_denial;
  d["ble_stopping"]=b.stopping;d["ble_teardown_failed"]=b.teardown_failed;
  d["ble_window_generation"]=b.window_generation;
  d["ble_connected_ms"]=b.connected_ms;d["ble_last_activity_ms"]=b.last_activity_ms;
  d["ble_error"]=b.last_error;d["lifecycle_error"]=status.error;
  d["ble_platform_ready"]=ble_platform_ready();d["ble_platform_error"]=ble_platform_error();
  const auto memory=ble_memory_status();
  d["ble_before_free_bytes"]=memory.before_free;
  d["ble_before_largest_bytes"]=memory.before_largest;
  d["ble_after_free_bytes"]=memory.after_free;
  d["ble_after_largest_bytes"]=memory.after_largest;
  d["ble_restore_checked"]=memory.checked;
  d["ble_stop_confirmed"]=memory.stop_confirmed;
  d["ble_memory_restored"]=memory.memory_restored;
}
String diagnostics_json(bool log){
  JsonDocument d;diagnostics_add_status(d);
#if CSP_BLE_ALLOC_TRACE
  const auto trace=ble_alloc_trace_summary();
  auto t=d["alloc_trace"].to<JsonObject>();
  t["snapshot"]=trace.snapshot;t["phase"]=uint32_t(trace.phase);
  t["allocations"]=trace.allocations;t["nested"]=trace.duplicates;
  t["unknown_frees"]=trace.unknown_frees;
  t["overflow"]=trace.overflow;t["failed_allocations"]=trace.failed;
  t["at_stop"]=trace.at_stop;t["still_live"]=trace.live;t["released_later"]=trace.released;
  t["live_bytes"]=trace.live_bytes;t["settle_ms"]=1000;
  t["denied_op"]=ble_alloc_trace_denied_call().operation;
  t["denied_caller"]=uint64_t(ble_alloc_trace_denied_call().caller);
  t["denied_phase"]=ble_alloc_trace_denied_call().phase;
  // Twelve records fit the existing bounded 8192-byte USB response. LOG cycles
  // pages of one immutable snapshot; it never changes settings or trace state.
  if(log&&trace.phase==pro::trace::Phase::Frozen){
    const auto span=ble_alloc_trace_heap_span();auto h=d["heap_span"].to<JsonObject>();
    h["captured"]=span.captured;h["overflow"]=span.overflow;
    h["heap_start"]=uint64_t(span.heap_start);h["heap_end"]=uint64_t(span.heap_end);
    h["before_start"]=uint64_t(span.before_start);h["before_bytes"]=span.before_bytes;
    h["after_start"]=uint64_t(span.after_start);h["after_bytes"]=span.after_bytes;
    h["global_after_start"]=uint64_t(span.global_after_start);h["global_after_bytes"]=span.global_after_bytes;
    h["barriers_count"]=span.barriers;auto blocks=h["barriers"].to<JsonArray>();
    for(uint32_t i=0;i<span.barriers;++i){auto b=blocks.add<JsonObject>();
      b["address"]=uint64_t(span.retained[i].address);b["bytes"]=span.retained[i].bytes;}
    static uint32_t cursor=0,snapshot=0;
    if(snapshot!=trace.snapshot){snapshot=trace.snapshot;cursor=0;}
    constexpr size_t PageSize=12;
    const uint32_t pages=trace.at_stop?(trace.at_stop+PageSize-1)/PageSize:1;
    cursor%=pages;t["page"]=cursor;t["pages"]=pages;
    pro::trace::Record records[PageSize];
    const size_t count=ble_alloc_trace_page(cursor*PageSize,records,PageSize);
    auto rows=t["records"].to<JsonArray>();
    for(size_t i=0;i<count;++i){const auto& r=records[i];auto row=rows.add<JsonObject>();
      row["address"]=uint64_t(r.address);row["caller"]=uint64_t(r.caller);
      row["bytes"]=r.bytes;row["sequence"]=r.sequence;
      row["phase"]=uint32_t(r.phase);row["api"]=uint32_t(r.api);row["live"]=r.live;
    }
    cursor=(cursor+1)%pages;
  }
#endif
  if(log){auto a=d["events"].to<JsonArray>();for(unsigned i=0;i<events.size();++i){
    auto e=events.at(i);auto row=a.add<JsonObject>();row["uptime_ms"]=e.ms;row["kind"]=e.kind;row["code"]=e.code;
  }}
  String out;serializeJson(d,out);return out;
}
void diagnostics_poll(){
  for(unsigned n=0;n<64&&USBSerial.available();++n){
    auto command=input.push(char(USBSerial.read()));if(command==pro::DiagnosticCommand::None)continue;
    if(lastReply&&uint32_t(millis()-lastReply)<200)continue;
    lastReply=millis();
    if(command==pro::DiagnosticCommand::Status||command==pro::DiagnosticCommand::Log)
      USBSerial.println(diagnostics_json(command==pro::DiagnosticCommand::Log));
    else USBSerial.println("{\"accepted\":false,\"reason\":\"Isolated test: STATUS/LOG only; hold physical BOOT for BLE\"}");
  }
}
#endif
