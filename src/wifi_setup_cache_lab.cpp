#include "wifi_setup_cache_lab.h"
#if CSP_NVS_CACHE_LAB
#include <esp_heap_caps.h>
#include <nvs.h>
namespace {bool attempted=false;}
WifiCacheLabResult wifi_setup_cache_lab(){
  WifiCacheLabResult out;
  if(attempted)return out;
  attempted=true;
  nvs_stats_t stats{};
  out.sdk_error=nvs_get_stats("nvs",&stats);
  if(out.sdk_error!=ESP_OK||stats.free_entries<40||stats.available_entries<40)return out;
  out.before_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  nvs_handle_t handle;
  out.sdk_error=nvs_open("csp-cache-lab",NVS_READWRITE,&handle);
  if(out.sdk_error!=ESP_OK)return out;
  // Repeated real SDK updates exercise its retained HashList cache across
  // append/erase entries. At most one harmless synthetic value remains stored.
  // Normal builds have no call or command for this service fixture.
  for(uint32_t i=0;i<32;++i){
    out.sdk_error=nvs_set_u32(handle,"probe",0x43535000u+i);
    if(out.sdk_error==ESP_OK)out.sdk_error=nvs_commit(handle);
    if(out.sdk_error!=ESP_OK)break;
    ++out.writes;
  }
  uint32_t check=0;
  if(out.sdk_error==ESP_OK)out.sdk_error=nvs_get_u32(handle,"probe",&check);
  nvs_close(handle);
  out.after_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  out.accepted=out.sdk_error==ESP_OK&&out.writes==32&&check==0x4353501fu;
  return out;
}
int32_t wifi_setup_cache_lab_clear(){
  if(!attempted)return ESP_ERR_INVALID_STATE;
  nvs_handle_t handle;
  auto result=nvs_open("csp-cache-lab",NVS_READWRITE,&handle);
  if(result!=ESP_OK)return result;
  result=nvs_erase_key(handle,"probe");
  if(result==ESP_ERR_NVS_NOT_FOUND){nvs_close(handle);return ESP_OK;}
  if(result==ESP_OK)result=nvs_commit(handle);
  uint32_t ignored=0;
  if(result==ESP_OK){
    const auto read=nvs_get_u32(handle,"probe",&ignored);
    result=read==ESP_ERR_NVS_NOT_FOUND?ESP_OK:read==ESP_OK?ESP_ERR_INVALID_STATE:read;
  }
  nvs_close(handle);return result;
}
#endif
