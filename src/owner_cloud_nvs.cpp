#include "owner_cloud_nvs.h"
#if CSP_ENABLE_OWNER_CLOUD_SETUP
#include <owner_cloud_store.h>
#include <nvs.h>
#include <cstring>
using pro::wifi_setup::Read;
namespace {bool configKey(const char* key){return key&&!std::strcmp(key,"config");}}
Read OwnerCloudNvs::read(const char* key,uint8_t* out,size_t capacity,size_t& size){
  size=0;if(!configKey(key)||!out||!capacity||capacity>pro::owner_cloud::BlobLimit)return Read::Error;
  nvs_handle_t handle;const auto opened=nvs_open("csp",NVS_READONLY,&handle);
  if(opened==ESP_ERR_NVS_NOT_FOUND)return Read::Missing;
  if(opened!=ESP_OK)return Read::Error;
  size_t length=0;auto code=nvs_get_blob(handle,"config",nullptr,&length);
  if(code==ESP_ERR_NVS_NOT_FOUND){nvs_close(handle);return Read::Missing;}
  if(code!=ESP_OK||!length||length>capacity){nvs_close(handle);return Read::Error;}
  const size_t expected=length;code=nvs_get_blob(handle,"config",out,&length);nvs_close(handle);
  if(code!=ESP_OK||length!=expected)return Read::Error;
  size=length;return Read::Found;
}
bool OwnerCloudNvs::write(const char* key,const uint8_t* bytes,size_t size){
  if(!configKey(key)||!bytes||!size||size>pro::owner_cloud::BlobLimit)return false;
  nvs_handle_t handle;if(nvs_open("csp",NVS_READWRITE,&handle)!=ESP_OK)return false;
  const bool stored=nvs_set_blob(handle,"config",bytes,size)==ESP_OK&&nvs_commit(handle)==ESP_OK;
  nvs_close(handle);return stored;
}
#endif
