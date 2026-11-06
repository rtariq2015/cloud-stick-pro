#include "owner_login_nvs.h"
#if CSP_ENABLE_OWNER_LOGIN
#include <owner_login_store.h>
#include <nvs.h>
#include <cstring>
using pro::wifi_setup::Read;
namespace {bool ownerKey(const char* key){return key&&!std::strcmp(key,"owner");}}
Read OwnerLoginNvs::read(const char* key,uint8_t* out,size_t capacity,size_t& size){
  size=0;if(!ownerKey(key)||!out||capacity!=pro::owner_login::RecordSize)return Read::Error;
  nvs_handle_t handle;const auto opened=nvs_open("csp-owner",NVS_READONLY,&handle);
  if(opened==ESP_ERR_NVS_NOT_FOUND)return Read::Missing;
  if(opened!=ESP_OK)return Read::Error;
  size_t length=0;auto status=nvs_get_blob(handle,"owner",nullptr,&length);
  if(status==ESP_ERR_NVS_NOT_FOUND){nvs_close(handle);return Read::Missing;}
  if(status!=ESP_OK||length!=capacity){nvs_close(handle);return Read::Error;}
  status=nvs_get_blob(handle,"owner",out,&length);nvs_close(handle);
  if(status!=ESP_OK||length!=capacity)return Read::Error;
  size=length;return Read::Found;
}
bool OwnerLoginNvs::write(const char* key,const uint8_t* bytes,size_t size){
  if(!ownerKey(key)||!bytes||size!=pro::owner_login::RecordSize)return false;
  nvs_handle_t handle;if(nvs_open("csp-owner",NVS_READWRITE,&handle)!=ESP_OK)return false;
  const bool stored=nvs_set_blob(handle,"owner",bytes,size)==ESP_OK&&nvs_commit(handle)==ESP_OK;
  nvs_close(handle);return stored;
}
#endif
