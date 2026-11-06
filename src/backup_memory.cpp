#include "backup_memory.h"
#if CSP_ENABLE_BACKUP_SETUP
#include <esp_heap_caps.h>
#include <atomic>
namespace {
uint32_t samples=0,minimumFree=UINT32_MAX,minimumLargest=UINT32_MAX;
std::atomic<bool> failureSeen{false};
bool registered=false;
uint32_t lastSample=0;
void failed(size_t,uint32_t,const char*){
  // Called inside allocator failure paths: no allocation, locks, logging,
  // scheduler calls, stack unwinding or attempts to change the failed request.
  failureSeen.store(true,std::memory_order_relaxed);
}
void sample(){
  constexpr auto caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  const auto free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
  if(free<minimumFree)minimumFree=free;
  if(largest<minimumLargest)minimumLargest=largest;
  if(samples!=UINT32_MAX)++samples;
}
}
void backup_memory_init(){
  registered=heap_caps_register_failed_alloc_callback(failed)==ESP_OK;
  sample();
}
void backup_memory_poll(uint32_t now){
  // Owner-loop caller only; bounded frequency independent of transfer size.
  if(uint32_t(now-lastSample)<25)return;
  lastSample=now;sample();
}
BackupMemorySnapshot backup_memory_snapshot(){
  BackupMemorySnapshot s;s.failure_hook_registered=registered;s.samples=samples;
  s.observed_min_free=samples?minimumFree:0;s.observed_min_largest=samples?minimumLargest:0;
  s.allocation_failure_seen=failureSeen.load(std::memory_order_relaxed);return s;
}
#endif
