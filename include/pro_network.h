#pragma once
#include <cstdint>
namespace pro {
constexpr uint32_t SetupMask=0xffffff00U;
constexpr uint32_t SetupAddresses[]={0xc0a80401U,0xac1ffe01U,0x0afdfe01U};
inline bool validMask(uint32_t mask){uint32_t host=~mask;return mask&&(host&(host+1))==0;}
inline bool networkOverlap(uint32_t ip,uint32_t mask,uint32_t other,uint32_t otherMask){
  uint32_t first=ip&mask,last=first|~mask,otherFirst=other&otherMask,otherLast=otherFirst|~otherMask;
  return first<=otherLast&&otherFirst<=last;
}
inline uint32_t chooseSetupAddress(uint32_t ip,uint32_t mask,uint32_t dns){
  if(!validMask(mask))return 0;
  for(uint32_t address:SetupAddresses)
    if(!networkOverlap(ip,mask,address,SetupMask)&&(!dns||((dns&SetupMask)!=(address&SetupMask))))return address;
  return 0;
}
}
