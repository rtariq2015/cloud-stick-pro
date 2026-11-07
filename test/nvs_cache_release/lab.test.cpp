// Real bounded service-fixture source; synthetic SDK, no real NVS or device.
#include "wifi_setup_cache_lab.h"
#include <esp_heap_caps.h>
#include <nvs.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
namespace {
int statsError=0,openError=0,writeFail=0,commitFail=0,readError=0,eraseError=0;
size_t freeEntries=64,availableEntries=64;
unsigned statsCalls=0,opens=0,writes=0,commits=0,reads=0,closes=0;
uint32_t value=0;
bool badRead=false,erased=false;
}
size_t heap_caps_get_largest_free_block(uint32_t caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));return writes?61284:61428;}
esp_err_t nvs_get_stats(const char* name,nvs_stats_t* out){assert(std::strcmp(name,"nvs")==0);++statsCalls;out->free_entries=freeEntries;out->available_entries=availableEntries;return statsError;}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* handle){assert(std::strcmp(name,"csp-cache-lab")==0&&mode==NVS_READWRITE);++opens;*handle=77;return openError;}
esp_err_t nvs_set_u32(nvs_handle_t handle,const char* key,uint32_t next){
  assert(handle==77&&std::strcmp(key,"probe")==0&&next==0x43535000u+writes);++writes;
  if(int(writes)==writeFail)return -42;
  value=next;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle){assert(handle==77);++commits;return int(commits)==commitFail?-43:ESP_OK;}
esp_err_t nvs_get_u32(nvs_handle_t handle,const char* key,uint32_t* out){assert(handle==77&&std::strcmp(key,"probe")==0);++reads;if(erased)return ESP_ERR_NVS_NOT_FOUND;*out=badRead?0:value;return readError;}
esp_err_t nvs_erase_key(nvs_handle_t handle,const char* key){assert(handle==77&&std::strcmp(key,"probe")==0);if(eraseError)return eraseError;erased=true;return ESP_OK;}
void nvs_close(nvs_handle_t handle){assert(handle==77);++closes;}
int main(int argc,char** argv){
  assert(argc==2);const std::string mode=argv[1];
  if(mode=="low-space")freeEntries=39;
  else if(mode=="reserved-space")availableEntries=39;
  else if(mode=="stats-fail")statsError=-40;
  else if(mode=="open-fail")openError=-41;
  else if(mode=="write-first-fail")writeFail=1;
  else if(mode=="write-later-fail")writeFail=17;
  else if(mode=="commit-fail")commitFail=17;
  else if(mode=="read-fail")readError=-44;
  else if(mode=="read-mismatch")badRead=true;
  else if(mode=="clear-erase-fail")eraseError=-45;
  else assert(mode=="normal");
  const auto out=wifi_setup_cache_lab();
  assert(out.accepted==(mode=="normal"||mode=="clear-erase-fail")&&statsCalls==1&&writes<=32&&commits<=32);
  if(out.accepted)assert(out.writes==32&&writes==32&&commits==32&&reads==1&&closes==1);
  if(mode=="low-space"||mode=="reserved-space"||mode=="stats-fail")assert(opens==0&&writes==0&&closes==0);
  else if(mode=="open-fail")assert(opens==1&&writes==0&&closes==0);
  else assert(closes==1);
  if(writeFail)assert(writes==unsigned(writeFail)&&out.writes==unsigned(writeFail-1)&&commits==out.writes&&reads==0);
  if(commitFail)assert(writes==unsigned(commitFail)&&out.writes==unsigned(commitFail-1)&&reads==0);
  const auto count=writes,stats=statsCalls;
  const auto repeated=wifi_setup_cache_lab();assert(!repeated.accepted&&writes==count&&statsCalls==stats);
  if(out.accepted){
    const auto clear=wifi_setup_cache_lab_clear();assert(clear==eraseError&&opens==2&&closes==2);
    assert(erased==(eraseError==0));
  }
  std::cout<<"PASS actual synthetic cache fixture "<<mode<<"; no user namespace access\n";
}
