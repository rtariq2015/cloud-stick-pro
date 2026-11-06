#include "public_wifi_probe.h"
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
#include "config_loader.h"
#include "webdav_client.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <WiFi.h>
#include <lwip/inet.h>
#include <atomic>
namespace pro::public_wifi {
namespace {
constexpr size_t StackBytes=16384;
portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
ProbeSnapshot snapshot;
TaskHandle_t owner=nullptr,task=nullptr;
BaseType_t core=-1;
StackType_t* stack=nullptr;StaticTask_t* control=nullptr;
std::atomic<bool> cancelled{false},handoff{false};
bool owns(){return !owner||(owner==xTaskGetCurrentTaskHandle()&&core==xPortGetCoreID()&&xTaskGetCoreID(owner)==core);}
bool cancel(void*){return cancelled.load(std::memory_order_acquire);}
struct Result {bool ok=false;int code=-2301;};
__attribute__((noinline)) Result run(){
  if(cancel(nullptr))return {false,-2010};
  auto config=load_backup_config();if(!config.is_valid)return {};
  WebDAVClient remote(config);
  const auto result=remote.probeConnectionReadOnly(cancel,nullptr);
  return {result.ok,result.code}; // remote, config and TLS have unwound on return.
}
void entry(void*){
  while(!handoff.load(std::memory_order_acquire))vTaskDelay(1);
  portENTER_CRITICAL(&mutex);snapshot.phase=ProbePhase::Running;portEXIT_CRITICAL(&mutex);
  const auto result=run();
  portENTER_CRITICAL(&mutex);snapshot.ok=result.ok;snapshot.code=result.code;snapshot.phase=ProbePhase::Drained;portEXIT_CRITICAL(&mutex);
  for(;;)vTaskSuspend(nullptr);
}
void failed(){
  heap_caps_free(stack);heap_caps_free(control);stack=nullptr;control=nullptr;
  portENTER_CRITICAL(&mutex);snapshot={ProbePhase::Failed,false,-2113};portEXIT_CRITICAL(&mutex);
}
}
bool probeStart(){
  if(!owns()||probeSnapshot().phase!=ProbePhase::Stopped||task||stack||control)return false;
  const auto caller=xTaskGetCurrentTaskHandle();const auto on=xPortGetCoreID();
  if(!caller||xTaskGetCoreID(caller)!=on)return false;
  owner=caller;core=on;cancelled=false;handoff=false;
  portENTER_CRITICAL(&mutex);snapshot={ProbePhase::Starting,false,0};portEXIT_CRITICAL(&mutex);
  stack=static_cast<StackType_t*>(heap_caps_malloc(StackBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  control=static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!stack||!control){failed();return false;}
  task=xTaskCreateStaticPinnedToCore(entry,"PortalProbe",StackBytes,nullptr,1,stack,control,core);
  if(!task){failed();return false;}
  handoff.store(true,std::memory_order_release);return true;
}
void probeCancel(){if(owns())cancelled.store(true,std::memory_order_release);}
ProbeSnapshot probeSnapshot(){portENTER_CRITICAL(&mutex);const auto copy=snapshot;portEXIT_CRITICAL(&mutex);return copy;}
bool probeReap(){
  if(!owns())return false;const auto current=probeSnapshot();
  if(current.phase==ProbePhase::Stopped)return true;
  if(current.phase==ProbePhase::Failed&&!task&&!stack&&!control){
    portENTER_CRITICAL(&mutex);snapshot.phase=ProbePhase::Stopped;portEXIT_CRITICAL(&mutex);return true;
  }
  if(current.phase!=ProbePhase::Drained||!task||eTaskGetState(task)!=eSuspended)return false;
  vTaskDelete(task);task=nullptr;heap_caps_free(stack);heap_caps_free(control);stack=nullptr;control=nullptr;
  portENTER_CRITICAL(&mutex);snapshot.phase=ProbePhase::Stopped;portEXIT_CRITICAL(&mutex);return true;
}
bool readUplink(pro::owned_guest::Uplink& out){
  esp_netif_ip_info_t info{};
  if(WiFi.status()!=WL_CONNECTED||!WiFi.STA.netif()||esp_netif_get_ip_info(WiFi.STA.netif(),&info)!=ESP_OK)return false;
  auto dns=WiFi.dnsIP(0);if(dns.type()!=IPv4||!dns[0])dns=WiFi.dnsIP(1);
  if(dns.type()!=IPv4)return false;
  out={ntohl(info.ip.addr),ntohl(info.netmask.addr),ntohl(info.gw.addr),
    uint32_t(dns[0])<<24|uint32_t(dns[1])<<16|uint32_t(dns[2])<<8|dns[3]};return true;
}
} // namespace pro::public_wifi
#endif
