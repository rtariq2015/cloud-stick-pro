#pragma once
#include "../wifi_setup_model/wifi_setup_model.h"
#include "../wifi_setup_model/owner_record.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace pro::owner_login {
using Error=pro::wifi_setup::Error;
using Access=pro::wifi_setup::Access;
constexpr size_t ChunkLimit=128;
constexpr uint32_t LifetimeMs=120000,OperationFreeFloor=48*1024,OperationLargestFloor=16*1024;
class Candidate {
 public:
  ~Candidate(){clear();}
  Error begin(const Access&,uint64_t transaction,uint64_t revision,uint32_t now);
  Error stage(const Access&,uint64_t transaction,uint32_t edit,uint32_t offset,const uint8_t*,size_t,uint32_t now);
  Error prepare(const Access&,uint64_t transaction,uint64_t revision,uint32_t edit,bool accept,const uint8_t mac[6],uint32_t now,std::array<uint8_t,432>&);
  Error abort(const Access&,uint64_t transaction);
  Error status(const Access&,uint32_t now);
  void clear();
  uint64_t transaction()const{return transaction_;}
  uint64_t revision()const{return revision_;}
  uint32_t edit()const{return edit_;}
  uint16_t received()const{return received_;}
 private:
  Error check(const Access&,uint64_t transaction,uint32_t now);
  std::array<uint8_t,432> bytes_{};
  uint64_t session_=0,transaction_=0,revision_=0;
  uint32_t started_=0,edit_=0;uint16_t received_=0;bool active_=false;
};
// Protected by the adapter's existing mailbox mutex. No SDK/allocator action is
// performed here; the owner task alone executes the resulting shutdown action.
class Handoff {
 public:
  static constexpr uint32_t AckWaitMs=5000,SettleMs=1000;
  bool arm(uint32_t id,uint64_t generation,uint32_t job,uint32_t now);
  bool matches(uint32_t id,uint64_t generation,uint32_t job)const;
  void committed(uint64_t revision){revision_=revision;durable_=true;}
  void uncertain(){fault_=true;}
  void encrypted(uint32_t id,uint64_t generation,uint32_t job,uint32_t now);
  bool shouldClose(uint32_t now,bool connected)const;
  void clear(){*this=Handoff{};}
  bool active()const{return active_;}
  bool fault()const{return fault_;}
  bool durable()const{return durable_;}
  bool acknowledged()const{return acknowledged_;}
  uint64_t revision()const{return revision_;}
  uint32_t job()const{return job_;}
 private:
  uint64_t generation_=0,revision_=0;
  uint32_t id_=0,job_=0,started_=0,acked_=0;
  bool active_=false,fault_=false,durable_=false,acknowledged_=false;
};
}
