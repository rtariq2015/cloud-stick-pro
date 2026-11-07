// Actual application cleanup against synthetic partition/SDK/cache observations.
#include "wifi_setup_nvs_release.h"
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
namespace {
constexpr size_t Bytes=0x5000;
std::array<uint8_t,Bytes> flash;
esp_partition_t partition{0x9000,Bytes};
bool present=true,denyAllocation=false,inRelease=false,mutate=false;
int deinitError=0,initError=0,failRead=0,reads=0,allocations=0,scrubbedFrees=0;
size_t finalLargest=118772;
uint8_t* snapshot=nullptr;
std::vector<std::string> calls;
}
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t bytes){
  if(inRelease&&bytes==Bytes){
    ++allocations;if(denyAllocation)return nullptr;
    assert(!snapshot);snapshot=static_cast<uint8_t*>(__real_malloc(bytes));return snapshot;
  }
  return __real_malloc(bytes);
}
extern "C" void __wrap_free(void* p){
  if(p&&p==snapshot){
    for(size_t i=0;i<Bytes;++i)assert(snapshot[i]==0);
    snapshot=nullptr;++scrubbedFrees;
  }
  __real_free(p);
}
size_t heap_caps_get_largest_free_block(uint32_t caps){
  assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(allocations){assert(!snapshot&&scrubbedFrees==1);return finalLargest;}
  return 61428;
}
const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* name){
  assert(type==ESP_PARTITION_TYPE_DATA&&subtype==ESP_PARTITION_SUBTYPE_DATA_NVS&&std::strcmp(name,"nvs")==0);
  return present?&partition:nullptr;
}
esp_err_t esp_partition_read(const esp_partition_t* p,size_t offset,void* out,size_t bytes){
  assert(p==&partition&&offset+bytes<=Bytes);++reads;
  if(reads==failRead)return -42;
  std::memcpy(out,flash.data()+offset,bytes);return ESP_OK;
}
esp_err_t nvs_flash_deinit(){assert(snapshot);calls.emplace_back("deinit");return deinitError;}
esp_err_t nvs_flash_init(){
  assert(snapshot&&calls.size()==1);calls.emplace_back("init");
  if(mutate)flash.back()^=1;
  return initError;
}
int main(int argc,char** argv){
  assert(argc==2);const std::string mode=argv[1];
  for(size_t i=0;i<Bytes;++i)flash[i]=uint8_t(i*17+63);
  const auto original=flash;
  if(mode=="missing")present=false;
  else if(mode=="wrong-address")partition.address=0xe000;
  else if(mode=="wrong-size")partition.size=Bytes+4096;
  else if(mode=="allocation-fail")denyAllocation=true;
  else if(mode=="read-before-fail")failRead=1;
  else if(mode=="read-after-fail")failRead=2;
  else if(mode=="read-tail-fail")failRead=81;
  else if(mode=="deinit-fail")deinitError=-43;
  else if(mode=="init-fail")initError=-44;
  else if(mode=="flash-changed")mutate=true;
  else if(mode=="heap-still-low")finalLargest=61428;
  else assert(mode=="normal");
  inRelease=true;const bool ok=wifi_setup_nvs_release_cache();inRelease=false;
  const auto s=wifi_setup_nvs_release_status();
  const bool expected=mode=="normal"||mode=="heap-still-low";
  assert(ok==expected&&s.flash_equal==expected&&!snapshot);
  if(expected){
    assert(calls==std::vector<std::string>({"deinit","init"}));assert(reads==81&&scrubbedFrees==1);
    assert(s.step==WifiNvsReleaseStep::Verified&&s.sdk_error==0&&s.before_largest==61428&&s.after_largest==finalLargest);
    // A successful cache rebuild is separate from the unchanged heap proof.
    assert(flash==original);
  }else{
    assert(s.step!=WifiNvsReleaseStep::Verified&&s.after_largest==0);
    assert(scrubbedFrees==(allocations&&!denyAllocation?1:0));
    if(mode=="missing"||mode=="wrong-address"||mode=="wrong-size"||mode=="allocation-fail"||mode=="read-before-fail")assert(calls.empty());
    if(mode=="deinit-fail")assert(calls==std::vector<std::string>({"deinit"})&&s.sdk_error==-43);
    if(mode=="init-fail")assert(calls.size()==2&&s.sdk_error==-44);
    if(mode=="flash-changed")assert(s.step==WifiNvsReleaseStep::Changed);
    else assert(flash==original);
  }
  std::cout<<"PASS actual NVS cache release "<<mode<<"; synthetic SDK/flash only\n";
}
