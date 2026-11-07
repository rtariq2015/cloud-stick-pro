#include "wifi_setup_nvs_release.h"
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
#include <wifi_setup_model.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <array>
#include <cstdlib>
#include <cstring>

namespace {
WifiNvsReleaseStatus status;
constexpr size_t NvsBytes=0x5000;
struct Snapshot {
  uint8_t* bytes=nullptr;
  ~Snapshot(){
    if(bytes){pro::wifi_setup::eraseSecretMemory(bytes,NvsBytes);std::free(bytes);}
  }
};
bool checked(esp_err_t error){status.sdk_error=error;return error==ESP_OK;}
}
WifiNvsReleaseStatus wifi_setup_nvs_release_status(){return status;}
bool wifi_setup_nvs_release_cache(){
  status={};status.before_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  status.step=WifiNvsReleaseStep::Partition;
  const auto* partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs");
  // This operation belongs to the unchanged reviewed Pro layout only.
  if(!partition||partition->address!=0x9000||partition->size!=NvsBytes)return false;
  status.step=WifiNvsReleaseStep::Allocate;
  {
    Snapshot before;before.bytes=static_cast<uint8_t*>(std::malloc(NvsBytes));
    if(!before.bytes)return false;
    status.step=WifiNvsReleaseStep::ReadBefore;
    if(!checked(esp_partition_read(partition,0,before.bytes,NvsBytes)))return false;
    // NVS HashList blocks can move into the temporary BLE arena during a save.
    // Use the SDK's public cache lifecycle; never free an SDK allocation by
    // address, erase NVS, rebase heap measurements or change stored settings.
    status.step=WifiNvsReleaseStep::Deinit;
    if(!checked(nvs_flash_deinit()))return false;
    status.step=WifiNvsReleaseStep::Init;
    if(!checked(nvs_flash_init()))return false;
    std::array<uint8_t,256> after{};
    for(size_t offset=0;offset<NvsBytes;offset+=after.size()){
      status.step=WifiNvsReleaseStep::ReadAfter;
      if(!checked(esp_partition_read(partition,offset,after.data(),after.size()))){
        pro::wifi_setup::eraseSecretMemory(after.data(),after.size());return false;
      }
      const bool equal=std::memcmp(before.bytes+offset,after.data(),after.size())==0;
      pro::wifi_setup::eraseSecretMemory(after.data(),after.size());
      if(!equal){status.step=WifiNvsReleaseStep::Changed;return false;}
    }
  } // Scrub and free the complete private snapshot before measuring recovery.
  status.flash_equal=true;status.step=WifiNvsReleaseStep::Verified;
  status.after_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  return true;
}
#endif
