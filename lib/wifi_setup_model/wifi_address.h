#pragma once
#include <cstdint>
namespace pro::wifi_setup {
inline bool validWifiLease(uint32_t ip,uint32_t mask,uint32_t gateway){
  const auto usable=[](uint32_t v){return (v>>24)!=0&&(v>>24)!=127&&(v>>24)<224&&v!=0xffffffffU;};
  const uint32_t hosts=~mask;
  if(!mask||!hosts||(hosts&(hosts+1))||!usable(ip)||!usable(gateway)||ip==gateway)return false;
  const uint32_t host=ip&hosts,gw=gateway&hosts;
  return host&&host!=hosts&&gw&&gw!=hosts&&(ip&mask)==(gateway&mask);
}
}
