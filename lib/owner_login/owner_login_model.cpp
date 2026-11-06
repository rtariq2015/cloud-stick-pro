#include "owner_login_model.h"
#include <cstring>
#include <limits>
namespace pro::owner_login {
namespace {uint32_t age(uint32_t now,uint32_t at){const auto n=uint32_t(now-at);return n<0x80000000u?n:0;}}
void Candidate::clear(){pro::wifi_setup::eraseSecretMemory(bytes_.data(),bytes_.size());session_=transaction_=revision_=0;started_=edit_=0;received_=0;active_=false;}
Error Candidate::begin(const Access& a,uint64_t transaction,uint64_t revision,uint32_t now){
  if(!pro::wifi_setup::Candidate::authorized(a))return Error::Denied;
  if(!a.workers_quiescent)return Error::WorkersBusy;if(active_)return Error::Busy;
  if(!transaction||!revision)return Error::Invalid;
  clear();session_=a.session;transaction_=transaction;revision_=revision;started_=now;active_=true;return Error::None;
}
Error Candidate::check(const Access& a,uint64_t transaction,uint32_t now){
  if(!pro::wifi_setup::Candidate::authorized(a)){if(a.session==session_)clear();return Error::Denied;}
  if(!a.workers_quiescent)return Error::WorkersBusy;
  if(!active_||a.session!=session_||!transaction||transaction!=transaction_)return Error::StaleTransaction;
  if(age(now,started_)>=LifetimeMs){clear();return Error::Expired;}return Error::None;
}
Error Candidate::stage(const Access& a,uint64_t transaction,uint32_t edit,uint32_t offset,const uint8_t* data,size_t size,uint32_t now){
  const auto state=check(a,transaction,now);if(state!=Error::None)return state;
  if(edit!=edit_||offset!=received_)return Error::StaleTransaction;
  if(edit_==std::numeric_limits<uint32_t>::max())return Error::Exhausted;
  if(!data||!size||size>ChunkLimit||size>bytes_.size()-received_)return Error::Invalid;
  std::memcpy(bytes_.data()+received_,data,size);received_+=uint16_t(size);++edit_;return Error::None;
}
Error Candidate::prepare(const Access& a,uint64_t transaction,uint64_t revision,uint32_t edit,bool accept,const uint8_t mac[6],uint32_t now,std::array<uint8_t,432>& out){
  const auto state=check(a,transaction,now);if(state!=Error::None)return state;
  if(revision!=revision_)return Error::StaleRevision;if(edit!=edit_)return Error::StaleTransaction;
  if(!accept)return Error::ConfirmationRequired;if(received_!=bytes_.size())return Error::Invalid;
  pro::wifi_setup::OwnerRecord decoded;
  const bool valid=pro::wifi_setup::decodeOwner(bytes_.data(),bytes_.size(),mac,decoded);
  pro::wifi_setup::eraseSecretMemory(&decoded,sizeof decoded);if(!valid)return Error::Invalid;
  out=bytes_;return Error::None;
}
Error Candidate::abort(const Access& a,uint64_t transaction){
  if(!pro::wifi_setup::Candidate::authorized(a))return Error::Denied;
  if(!a.workers_quiescent)return Error::WorkersBusy;
  if(!active_||a.session!=session_||!transaction||transaction!=transaction_)return Error::StaleTransaction;
  clear();return Error::None;
}
Error Candidate::status(const Access& a,uint32_t now){return check(a,transaction_,now);}
bool Handoff::arm(uint32_t id,uint64_t generation,uint32_t job,uint32_t now){
  if(active_||!generation||!job)return false;active_=true;id_=id;generation_=generation;job_=job;started_=now;return true;
}
bool Handoff::matches(uint32_t id,uint64_t generation,uint32_t job)const{return active_&&generation==generation_&&id==id_&&job&&job==job_;}
void Handoff::encrypted(uint32_t id,uint64_t generation,uint32_t job,uint32_t now){
  if(matches(id,generation,job)&&!acknowledged_){acknowledged_=true;acked_=now;}
}
bool Handoff::shouldClose(uint32_t now,bool connected)const{
  return active_&&(!connected||age(now,started_)>=AckWaitMs||(acknowledged_&&age(now,acked_)>=SettleMs));
}
}
