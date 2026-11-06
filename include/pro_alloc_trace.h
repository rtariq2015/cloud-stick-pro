#pragma once
#include <cstddef>
#include <cstdint>

// Caller supplies synchronization. No allocation, payload access or SDK calls.
// Hook methods are always inline so their target caller remains entirely IRAM.
#define CSP_TRACE_INLINE inline __attribute__((always_inline))
namespace pro::trace {
enum class Phase : uint8_t { Idle, Start, Ready, Authenticated, Settling, Frozen };
enum class Api : uint8_t { Malloc=1, Calloc, Realloc, HeapMalloc, HeapCalloc, HeapRealloc, Aligned,
                           LwipPool, LwipMem, HeapDefault, MultiHeap, EspTimer };
struct Record {
  uintptr_t address=0,caller=0;
  size_t bytes=0;
  uint32_t sequence=0;
  Phase phase=Phase::Idle;
  Api api=Api::Malloc;
  bool live=false,at_stop=false;
};
struct Summary {
  uint32_t snapshot=0,allocations=0,duplicates=0,unknown_frees=0,overflow=0,failed=0;
  uint32_t at_stop=0,live=0,released=0;
  uint64_t live_bytes=0;
  Phase phase=Phase::Idle;
};
template<size_t Capacity> class Tracker {
 public:
  static constexpr size_t capacity=Capacity;
  void begin(){
    const uint32_t id=summary_.snapshot+1;
    for(auto& r:records_)r=Record{};
    summary_=Summary{};summary_.snapshot=id;summary_.phase=Phase::Start;
  }
  CSP_TRACE_INLINE bool capturing()const{
    // Keep recording replacements until the heap walker/freeze finishes.
    // Otherwise a timer reallocated during settlement is an invisible barrier.
    return summary_.phase>=Phase::Start&&summary_.phase<=Phase::Settling;
  }
  CSP_TRACE_INLINE Phase current_phase()const{return summary_.phase;}
  CSP_TRACE_INLINE void phase(Phase p){if(capturing()&&p>=summary_.phase&&p<=Phase::Authenticated)summary_.phase=p;}
  CSP_TRACE_INLINE void allocation(uintptr_t address,size_t bytes,uintptr_t caller,Api api){
    if(!capturing())return;
    if(!address){++summary_.failed;return;}
    ++summary_.allocations;
    Record* empty=nullptr;
    for(auto& r:records_){
      if(r.live&&r.address==address){
        // Nested malloc -> heap_caps_malloc wrappers describe one allocation.
        r.bytes=bytes;r.caller=caller;r.api=api;++summary_.duplicates;return;
      }
      if(!r.live&&!empty)empty=&r;
    }
    if(!empty){++summary_.overflow;return;}
    *empty={address,caller,bytes,summary_.allocations,summary_.phase,api,true,
            summary_.phase==Phase::Settling};
  }
  CSP_TRACE_INLINE Record take(uintptr_t address){
    if(!address||!capturing())return {};
    for(auto& r:records_)if(r.live&&r.address==address){const Record saved=r;r=Record{};return saved;}
    return {};
  }
  // A network pool can wrap a tracked heap allocation. Retain the measured
  // byte count while assigning its outer caller. Static pool sizes are unknown
  // here and remain zero; never read an allocation's payload to infer its size.
  CSP_TRACE_INLINE void annotate(uintptr_t address,uintptr_t caller,Api api){
    if(!capturing()||!address)return;
    for(auto& r:records_)if(r.live&&r.address==address){r.caller=caller;r.api=api;return;}
    allocation(address,0,caller,api);
  }
  CSP_TRACE_INLINE void restore(const Record& saved){
    if(!saved.live||!capturing())return;
    for(auto& r:records_)if(!r.live){r=saved;return;}
    ++summary_.overflow;
  }
  CSP_TRACE_INLINE void release(uintptr_t address){
    if(!address||summary_.phase==Phase::Idle||summary_.phase==Phase::Frozen)return;
    for(auto& r:records_)if(r.live&&r.address==address){
      r.live=false;if(summary_.phase!=Phase::Settling)r=Record{};return;
    }
    ++summary_.unknown_frees;
  }
  void stop(){
    if(!capturing()||summary_.phase==Phase::Settling)return;
    for(auto& r:records_)if(r.live)r.at_stop=true;
    summary_.phase=Phase::Settling;
  }
  void freeze(){if(summary_.phase==Phase::Settling)summary_.phase=Phase::Frozen;}
  Summary summary()const{
    // Export count includes initial stop survivors and late settlement objects.
    // Recompute after slot reuse/take/restore so pagination remains exact.
    Summary s=summary_;s.at_stop=0;
    for(const auto& r:records_)if(r.at_stop){
      ++s.at_stop;
      if(r.live){++s.live;s.live_bytes+=r.bytes;}else ++s.released;
    }
    return s;
  }
  size_t page(size_t offset,Record* out,size_t limit)const{
    if(summary_.phase!=Phase::Frozen||!out)return 0;
    size_t seen=0,written=0;
    for(const auto& r:records_)if(r.at_stop){
      if(seen++<offset)continue;
      if(written==limit)break;
      out[written++]=r;
    }
    return written;
  }
 private:
  Record records_[Capacity]{};
  Summary summary_{};
};
}
#undef CSP_TRACE_INLINE
