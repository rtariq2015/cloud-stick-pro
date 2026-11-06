#include "backup_memory.h"
#include <esp_heap_caps.h>
#include <cassert>
#include <thread>
#include <vector>
#include <cstring>
esp_alloc_failed_hook_t hook=nullptr;
bool reject=false;
size_t available=100000,largest=64000;
int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t f){if(reject)return -1;hook=f;return 0;}
size_t heap_caps_get_free_size(uint32_t caps){assert(caps==3);return available;}
size_t heap_caps_get_largest_free_block(uint32_t caps){assert(caps==3);return largest;}
int main(int argc,char** argv){
  reject=argc>1&&!std::strcmp(argv[1],"hook-failure");
  auto before=backup_memory_snapshot();assert(!before.samples&&!before.failure_hook_registered);
  backup_memory_init();auto initial=backup_memory_snapshot();
  assert(initial.samples==1&&initial.observed_min_free==100000&&initial.observed_min_largest==64000);
  assert(initial.failure_hook_registered==!reject);
  available=40000;largest=20000;backup_memory_poll(24);
  assert(backup_memory_snapshot().samples==1);
  backup_memory_poll(25);available=90000;largest=18000;backup_memory_poll(50);
  auto low=backup_memory_snapshot();assert(low.samples==3&&low.observed_min_free==40000&&low.observed_min_largest==18000);
  // Region-independent sampled totals never inherit the SDK's unrelated
  // cumulative region low-water values; no such API is linked by this test.
  available=50000;largest=30000;backup_memory_poll(UINT32_MAX-10);backup_memory_poll(20);
  auto wrapped=backup_memory_snapshot();assert(wrapped.samples==5&&wrapped.observed_min_free==40000);
  if(!reject){
    assert(hook);std::vector<std::thread> threads;
    for(unsigned t=0;t<4;++t)threads.emplace_back([]{for(unsigned i=0;i<1000;++i)hook(1024,3,"synthetic-allocator");});
    for(auto& thread:threads)thread.join();assert(backup_memory_snapshot().allocation_failure_seen);
  }else assert(!hook&&!backup_memory_snapshot().allocation_failure_seen);
}
