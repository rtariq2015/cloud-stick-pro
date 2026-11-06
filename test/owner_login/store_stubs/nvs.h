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
namespace owner_nvs_fake {
inline std::string blob,pending;
inline bool present=false,namespace_present=true,opened=false,apply_set_error=false,apply_commit_error=false,corrupt_readback=false;
inline int mode=0,open_result=ESP_OK,query_result=ESP_OK,read_result=ESP_OK,set_result=ESP_OK,commit_result=ESP_OK;
inline size_t query_override=0,read_override=0;
inline bool use_query_override=false,use_read_override=false;
inline unsigned opens=0,closes=0,queries=0,reads=0,writes=0,commits=0,fail_read_at=0,fail_query_at=0,fail_open_at=0;
}
inline int nvs_open(const char* name,int mode,nvs_handle_t* out){
  using namespace owner_nvs_fake;assert(!std::strcmp(name,"csp-owner")&&!opened);++opens;
  if(open_result!=ESP_OK||opens==fail_open_at)return ESP_FAIL;
  if(mode==NVS_READONLY&&!namespace_present)return ESP_ERR_NVS_NOT_FOUND;
  assert(mode==NVS_READONLY||mode==NVS_READWRITE);owner_nvs_fake::mode=mode;opened=true;*out=9;return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle){using namespace owner_nvs_fake;assert(handle==9&&opened);opened=false;++closes;}
inline int nvs_get_blob(nvs_handle_t handle,const char* key,void* out,size_t* size){
  using namespace owner_nvs_fake;assert(handle==9&&opened&&!std::strcmp(key,"owner"));
  if(!present)return ESP_ERR_NVS_NOT_FOUND;
  if(!out){++queries;*size=use_query_override?query_override:blob.size();return queries==fail_query_at?ESP_FAIL:query_result;}
  ++reads;const auto n=std::min(*size,blob.size());std::memcpy(out,blob.data(),n);
  if(corrupt_readback&&writes&&n)static_cast<char*>(out)[n-1]^=1;
  *size=use_read_override?read_override:blob.size();return reads==fail_read_at?ESP_FAIL:read_result;
}
inline int nvs_set_blob(nvs_handle_t handle,const char* key,const void* bytes,size_t size){
  using namespace owner_nvs_fake;assert(handle==9&&opened&&mode==NVS_READWRITE&&!std::strcmp(key,"owner")&&size==432);++writes;
  pending.assign(static_cast<const char*>(bytes),size);
  if(set_result!=ESP_OK&&apply_set_error){blob=pending;present=true;}return set_result;
}
inline int nvs_commit(nvs_handle_t handle){
  using namespace owner_nvs_fake;assert(handle==9&&opened&&mode==NVS_READWRITE);++commits;
  if(commit_result==ESP_OK||apply_commit_error){blob=pending;present=true;}return commit_result;
}
// No erase, reset, formatting, key enumeration or other namespaces are exposed.
