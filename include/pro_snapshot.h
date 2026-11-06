#pragma once
#include <cstdint>
namespace pro {
// Caller serializes this guard with every physical SD read/write. A USB write
// invalidates the read session before touching any sector, including metadata.
class SnapshotGuard {
  uint64_t generation_=0,captured_=0;uint32_t lastWrite_=0;bool active_=false;
 public:
  void wrote(uint32_t now){++generation_;lastWrite_=now;}
  void invalidate(){++generation_;}
  uint32_t remaining(uint32_t now,uint32_t quiet)const{uint32_t elapsed=now-lastWrite_;return elapsed>=quiet?0:quiet-elapsed;}
  bool begin(uint32_t now,uint32_t quiet){if(active_||remaining(now,quiet))return false;captured_=generation_;active_=true;return true;}
  bool valid()const{return active_&&captured_==generation_;}
  bool active()const{return active_;}
  void end(){active_=false;}
  uint64_t generation()const{return generation_;}
};
inline bool inBackupWindow(unsigned hour,unsigned start,unsigned end){
  if(hour>23||start>23||end>23)return false;
  return start==end||(start<end?(hour>=start&&hour<end):(hour>=start||hour<end));
}
}
