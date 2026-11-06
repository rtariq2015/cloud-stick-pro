#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace pro::scan {
// RAM-only proof, owned by one worker or its fully stopped pinned owner. It
// contains no filenames/credentials and never writes receipts or media.
struct Summary {
  uint32_t verified=0,pending=0,failed=0,skipped=0,transferred=0;
  uint64_t lastBackup=0;
  bool hasScan=false,complete=false;
  int error=0;
};
inline bool digest(const std::string& value){
  if(value.size()!=64)return false;
  for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
  return true;
}
class IdleGate {
 public:
  static constexpr uint32_t AuditMs=24u*60u*60u*1000u;
  void invalidate(){valid_=false;scope_.fill(0);metadata_.fill(0);}
  bool valid()const{return valid_;}
  // A receipt inventory is reread on worker restart, before reusing RAM proof.
  // Clearing status externally must force a scan, never restore cached counts.
  void resume(uint64_t generation,const std::string& scope,const std::string& metadata,
              const Summary& current,bool mediaReady){
    if(!coherent(generation,scope,current,mediaReady)||!digest(metadata)||
       std::memcmp(metadata_.data(),metadata.data(),64))invalidate();
  }
  bool skip(uint64_t generation,const std::string& scope,uint32_t now,
            const Summary& current,bool mediaReady){
    if(!coherent(generation,scope,current,mediaReady)||uint32_t(now-auditedAt_)>=AuditMs){
      invalidate();return false;
    }
    return true;
  }
  bool completed(uint64_t begin,uint64_t end,const std::string& scope,
                 const std::string& metadata,uint32_t now,const Summary& current,
                 uint32_t seen,bool entireScan,bool mediaReady){
    invalidate();
    if(begin!=end||!entireScan||!mediaReady||!digest(scope)||!digest(metadata)||
       !clean(current)||current.verified!=seen)return false;
    generation_=begin;auditedAt_=now;summary_=current;
    std::memcpy(scope_.data(),scope.data(),64);
    std::memcpy(metadata_.data(),metadata.data(),64);valid_=true;return true;
  }
 private:
  bool valid_=false;
  uint64_t generation_=0;
  uint32_t auditedAt_=0;
  std::array<char,65> scope_{},metadata_{};
  Summary summary_{};
  static bool clean(const Summary& value){
    return value.hasScan&&value.complete&&!value.pending&&!value.failed&&
      !value.skipped&&!value.error;
  }
  bool coherent(uint64_t generation,const std::string& scope,const Summary& current,bool mediaReady)const{
    return valid_&&mediaReady&&generation==generation_&&digest(scope)&&
      !std::memcmp(scope_.data(),scope.data(),64)&&clean(current)&&
      current.verified==summary_.verified&&current.transferred==summary_.transferred&&
      current.lastBackup==summary_.lastBackup;
  }
};
static_assert(sizeof(IdleGate)<=256,"Idle scan proof must remain small and bounded");
}
