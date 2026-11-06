#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
using nvs_handle_t=unsigned;
using esp_err_t=int;
constexpr esp_err_t ESP_OK=0,ESP_FAIL=-1;
constexpr int NVS_READONLY=1;
namespace config_fake {
inline std::string blob;
inline size_t queryOverride=0,readOverride=0;
inline bool useQueryOverride=false,useReadOverride=false;
inline int openResult=ESP_OK,queryResult=ESP_OK,readResult=ESP_OK;
inline unsigned opens=0,closes=0,queries=0,reads=0,writes=0,erases=0;
inline bool opened=false;
}
inline esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* handle){
  assert(!std::strcmp(name,"csp")&&mode==NVS_READONLY&&!config_fake::opened);
  ++config_fake::opens;
  if(config_fake::openResult)return config_fake::openResult;
  config_fake::opened=true;*handle=7;return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle){assert(handle==7&&config_fake::opened);config_fake::opened=false;++config_fake::closes;}
inline esp_err_t nvs_get_blob(nvs_handle_t handle,const char* key,void* output,size_t* size){
  assert(handle==7&&config_fake::opened&&!std::strcmp(key,"config"));
  if(!output){
    ++config_fake::queries;*size=config_fake::useQueryOverride?config_fake::queryOverride:config_fake::blob.size();
    return config_fake::queryResult;
  }
  ++config_fake::reads;
  const auto copied=std::min(*size,config_fake::blob.size());
  std::memcpy(output,config_fake::blob.data(),copied);
  *size=config_fake::useReadOverride?config_fake::readOverride:config_fake::blob.size();
  return config_fake::readResult;
}
