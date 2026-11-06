#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
using nvs_handle_t=unsigned;
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NVS_NOT_FOUND=0x1102;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
namespace config_fake {
inline std::string blob,pending;
inline bool present=false,opened=false,applySetError=false,applyCommitError=false,corruptReadback=false;
inline int mode=0,openResult=ESP_OK,queryResult=ESP_OK,readResult=ESP_OK,setResult=ESP_OK,commitResult=ESP_OK;
inline size_t queryOverride=0,readOverride=0;
inline bool useQueryOverride=false,useReadOverride=false;
inline unsigned opens=0,closes=0,queries=0,reads=0,writes=0,erases=0,commits=0;
}
inline int nvs_open(const char* name,int mode,nvs_handle_t* out){
  using namespace config_fake;assert(!std::strcmp(name,"csp")&&!opened);++opens;
  if(openResult!=ESP_OK)return openResult;
  if(mode==NVS_READONLY&&!present)return ESP_ERR_NVS_NOT_FOUND;
  assert(mode==NVS_READONLY||mode==NVS_READWRITE);config_fake::mode=mode;opened=true;*out=7;return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle){using namespace config_fake;assert(handle==7&&opened);opened=false;++closes;}
inline int nvs_get_blob(nvs_handle_t handle,const char* key,void* out,size_t* size){
  using namespace config_fake;assert(handle==7&&opened&&!std::strcmp(key,"config"));
  if(!present)return ESP_ERR_NVS_NOT_FOUND;
  if(!out){++queries;*size=useQueryOverride?queryOverride:blob.size();return queryResult;}
  ++reads;const auto capacity=*size;const auto n=std::min(capacity,blob.size());std::memcpy(out,blob.data(),n);
  if(corruptReadback&&writes&&n)static_cast<char*>(out)[n-1]^=1;
  *size=useReadOverride?readOverride:blob.size();return readResult;
}
inline int nvs_set_blob(nvs_handle_t handle,const char* key,const void* bytes,size_t size){
  using namespace config_fake;assert(handle==7&&opened&&mode==NVS_READWRITE&&!std::strcmp(key,"config"));++writes;
  pending.assign(static_cast<const char*>(bytes),size);
  if(setResult!=ESP_OK&&applySetError){blob=pending;present=true;}
  return setResult;
}
inline int nvs_commit(nvs_handle_t handle){
  using namespace config_fake;assert(handle==7&&opened&&mode==NVS_READWRITE);++commits;
  if(commitResult==ESP_OK||applyCommitError){blob=pending;present=true;}
  return commitResult;
}
