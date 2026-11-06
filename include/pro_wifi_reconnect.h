#pragma once
#include <wifi_profile_store.h>
#include <array>
#include <cstdint>

namespace pro {
// Owner-loop policy only: no radio, credentials, storage or blocking work.
// Keep a working link; consider another saved profile only after disconnection.
class WifiReconnect {
  struct Failure {uint32_t id=0,until=0;uint8_t count=0;};
  std::array<Failure,8> failures_{};
  uint64_t revision_=0;
 public:
  void revision(uint64_t value){if(revision_!=value){failures_={};revision_=value;}}
  void failed(uint32_t id,uint32_t now){
    if(!id)return;
    Failure* entry=nullptr;
    for(auto& item:failures_)if(item.id==id){entry=&item;break;}
    if(!entry)for(auto& item:failures_)if(!item.id){entry=&item;item.id=id;break;}
    if(!entry)return;
    if(entry->count<6)++entry->count;
    const uint32_t seconds=entry->count>=6?300:10u<<(entry->count-1);
    entry->until=now+seconds*1000;
  }
  void connected(uint32_t id){for(auto& item:failures_)if(item.id==id){item={};break;}}
  bool available(uint32_t id,uint32_t now)const{
    for(const auto& item:failures_)if(item.id==id)return int32_t(now-item.until)>=0;
    return true;
  }
  uint32_t choose(const wifi_setup::StoredNetworks& networks,uint32_t now)const{
    const wifi_setup::StoredProfile* best=nullptr;
    if(networks.count>networks.profiles.size())return 0;
    for(size_t i=0;i<networks.count;++i){
      const auto& item=networks.profiles[i];
      if(!item.id||!item.enabled||!available(item.id,now))continue;
      if(!best||item.id==networks.selected||
         (best->id!=networks.selected&&(item.priority>best->priority||
          (item.priority==best->priority&&item.id<best->id))))best=&item;
    }
    return best?best->id:0;
  }
};
}
