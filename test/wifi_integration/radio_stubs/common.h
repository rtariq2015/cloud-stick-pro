#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <arpa/inet.h>
using esp_err_t=int;using esp_event_base_t=const char*;using esp_event_handler_instance_t=void*;
using TickType_t=uint32_t;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NOT_FOUND=0x105,ESP_ERR_TIMEOUT=0x107,ESP_ERR_WIFI_NOT_STARTED=0x3002,
 ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED=0x5001,ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED=0x5002,ESP_ERR_ESP_NETIF_DHCP_NOT_STOPPED=0x5003;
constexpr int WIFI_STA=1,WIFI_OFF=0,WIFI_MODE_NULL=0,WIFI_STORAGE_RAM=1,WIFI_IF_STA=0,WIFI_SCAN_TYPE_ACTIVE=0,WIFI_ALL_CHANNEL_SCAN=1,ESP_ERR_WIFI_NOT_INIT=0x3001;
using wifi_mode_t=int;
inline bool driverInitialized=false;
constexpr int ESP_EVENT_ANY_ID=-1,WIFI_EVENT_SCAN_DONE=1,WIFI_EVENT_STA_DISCONNECTED=2,
 IP_EVENT_STA_GOT_IP=3,IP_EVENT_STA_LOST_IP=4;
constexpr int WIFI_REASON_AUTH_FAIL=202,WIFI_REASON_HANDSHAKE_TIMEOUT=204,WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT=15;
enum wifi_auth_mode_t {WIFI_AUTH_OPEN=0,WIFI_AUTH_WPA2_PSK=3,WIFI_AUTH_WPA_WPA2_PSK=4,WIFI_AUTH_WPA3_PSK=6,WIFI_AUTH_WPA2_WPA3_PSK=7,WIFI_AUTH_ENTERPRISE=5};
inline constexpr const char* WIFI_EVENT="wifi";inline constexpr const char* IP_EVENT="ip";
#define ESP_EVENT_DEFINE_BASE(name) const char* name=#name
struct wifi_ap_record_t {uint8_t ssid[33]{};int8_t rssi=0;uint8_t primary=1;wifi_auth_mode_t authmode=WIFI_AUTH_WPA2_PSK;};
struct wifi_event_sta_disconnected_t {int reason=0;};
struct wifi_scan_config_t {bool show_hidden=false;int scan_type=0;struct {struct {unsigned min=0,max=0;} active;} scan_time;};
struct wifi_config_t {struct {uint8_t ssid[32]{},password[64]{};struct {wifi_auth_mode_t authmode=WIFI_AUTH_OPEN;} threshold;int scan_method=0,failure_retry_cnt=0;} sta;};
struct esp_ip4_addr_t {uint32_t addr=0;};
struct esp_netif_ip_info_t {esp_ip4_addr_t ip,netmask,gw;};
struct esp_netif_t {int dummy=0;};
using callback_t=void(*)(void*,esp_event_base_t,int32_t,void*);
struct Registered {esp_event_base_t base;int id;callback_t fn;void* arg;};
extern std::vector<Registered> callbacks;
extern std::vector<wifi_ap_record_t> scanResults;
extern uint32_t testClock;
extern std::string failFunction;
extern bool driverStarted,associated,dhcpStarted,listFreed,dropBarrier;
extern bool pendingDhcpReset,dhcpStopped;
extern wifi_config_t driverConfig;
extern wifi_ap_record_t associatedAP;
extern esp_netif_ip_info_t lease;
extern unsigned sdkCalls,connectCalls,stopCalls,storageMode;
inline uint32_t millis(){return testClock;}
inline uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
inline void vTaskDelay(uint32_t n){testClock+=n;}
inline bool failure(const char* name){++sdkCalls;return failFunction==name;}
inline void emit(esp_event_base_t base,int id,void* payload=nullptr){for(const auto& c:callbacks)if(c.base==base&&(c.id==id||c.id==ESP_EVENT_ANY_ID))c.fn(c.arg,base,id,payload);}
struct FakeWiFi {
 void persistent(bool p){if(p)std::abort();}
 void setAutoReconnect(bool p){if(p)std::abort();}
 bool mode(int m){
  if(m==WIFI_OFF){if(failure("mode-off")||!driverStarted)return false;driverStarted=driverInitialized=false;return true;}
  driverStarted=driverInitialized=true;return !failure("mode");
 }
};extern FakeWiFi WiFi;
inline int esp_wifi_set_storage(int mode){storageMode=mode;return failure("storage")?ESP_FAIL:ESP_OK;}
inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char*){static esp_netif_t netif;return failure("netif")?nullptr:&netif;}
inline int esp_event_handler_instance_register(esp_event_base_t base,int id,callback_t fn,void* arg,void** out){if(failure("register"))return ESP_FAIL;callbacks.push_back({base,id,fn,arg});*out=reinterpret_cast<void*>(callbacks.size());return ESP_OK;}
inline int esp_event_handler_instance_unregister(esp_event_base_t base,int id,void*){
 if(failure("unregister"))return ESP_FAIL;
 for(auto it=callbacks.begin();it!=callbacks.end();++it)if(it->base==base&&it->id==id){callbacks.erase(it);return ESP_OK;}
 return ESP_ERR_NOT_FOUND;
}
inline int esp_wifi_get_mode(wifi_mode_t* mode){if(failure("release-proof")){*mode=WIFI_STA;return ESP_OK;}return driverInitialized?ESP_OK:ESP_ERR_WIFI_NOT_INIT;}
inline int esp_event_post(esp_event_base_t base,int id,const void* payload,size_t,TickType_t){if(failure("post"))return ESP_FAIL;if(!dropBarrier){if(pendingDhcpReset){dhcpStopped=false;dhcpStarted=false;pendingDhcpReset=false;}emit(base,id,const_cast<void*>(payload));}return ESP_OK;}
inline int esp_wifi_disconnect(){associated=false;return ESP_OK;}
inline int esp_wifi_stop(){++stopCalls;if(failure("stop"))return ESP_FAIL;pendingDhcpReset|=dhcpStarted;driverStarted=false;associated=false;return ESP_OK;}
inline int esp_wifi_start(){if(failure("start"))return ESP_FAIL;driverStarted=true;return ESP_OK;}
inline int esp_netif_dhcpc_stop(esp_netif_t*){if(failure("dhcp-stop"))return ESP_FAIL;dhcpStarted=false;dhcpStopped=true;return ESP_OK;}
inline int esp_netif_dhcpc_start(esp_netif_t*){if(failure("dhcp-start"))return ESP_FAIL;dhcpStarted=true;dhcpStopped=false;return ESP_OK;}
inline int esp_netif_set_ip_info(esp_netif_t*,const esp_netif_ip_info_t* address){if(failure("set-ip"))return ESP_FAIL;if(!dhcpStopped)return ESP_ERR_ESP_NETIF_DHCP_NOT_STOPPED;lease=*address;return ESP_OK;}
inline int esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t* out){if(failure("get-ip"))return ESP_FAIL;*out=lease;return ESP_OK;}
inline int esp_wifi_set_config(int,const wifi_config_t* p){if(failure("config"))return ESP_FAIL;driverConfig=*p;return ESP_OK;}
inline int esp_wifi_connect(){++connectCalls;return failure("connect")?ESP_FAIL:ESP_OK;}
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t* out){if(!associated)return ESP_FAIL;*out=associatedAP;return ESP_OK;}
inline int esp_wifi_scan_start(const wifi_scan_config_t*,bool blocking){if(blocking)std::abort();listFreed=false;return failure("scan-start")?ESP_FAIL:ESP_OK;}
inline int esp_wifi_clear_ap_list(){if(failure("clear-list"))return ESP_FAIL;if(!driverStarted)return ESP_ERR_WIFI_NOT_STARTED;listFreed=true;return ESP_OK;}
inline int esp_wifi_scan_stop(){if(failure("scan-stop"))return ESP_FAIL;emit(WIFI_EVENT,WIFI_EVENT_SCAN_DONE);return ESP_OK;}
inline int esp_wifi_scan_get_ap_num(uint16_t* n){*n=scanResults.size();return failure("scan-number")?ESP_FAIL:ESP_OK;}
inline int esp_wifi_scan_get_ap_records(uint16_t* n,wifi_ap_record_t* out){if(failure("scan-records"))return ESP_FAIL;*n=std::min<size_t>(*n,scanResults.size());for(size_t i=0;i<*n;++i)out[i]=scanResults[i];return ESP_OK;}
