#pragma once
#include <cstdint>
#include <algorithm>
namespace pro {
enum class TransferPhase:uint8_t { None, Reading, Uploading, Checking, Publishing };
class TransferMeter {
  uint32_t sampledAt_=0;uint64_t sampledBytes_=0;
 public:
  uint64_t done=0,total=0;uint32_t bytesPerSecond=0;
  void start(uint64_t size,uint32_t now){done=sampledBytes_=0;total=size;bytesPerSecond=0;sampledAt_=now;}
  void update(uint64_t bytes,uint32_t now){
    done=std::min(bytes,total);if(done<sampledBytes_){sampledBytes_=done;sampledAt_=now;bytesPerSecond=0;return;}uint32_t elapsed=now-sampledAt_;
    if(elapsed>=500){bytesPerSecond=uint32_t(std::min<uint64_t>(UINT32_MAX,(done-sampledBytes_)*1000/elapsed));sampledBytes_=done;sampledAt_=now;}
  }
  int percent()const{return total?int(done*100/total):100;}
};
}
