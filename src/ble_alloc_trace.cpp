#include "ble_alloc_trace.h"
#if CSP_BLE_ALLOC_TRACE
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <atomic>
#if CSP_USB_BENCH_AUTOMATION
#include <lwip/memp.h>
#include <lwip/mem.h>
#include <sys/reent.h>
#include <multi_heap.h>
#include <esp_timer.h>
#endif

namespace {
using pro::trace::Api;
using pro::trace::Phase;
using pro::trace::Record;
DRAM_ATTR pro::trace::Tracker<256> tracker;
DRAM_ATTR portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR std::atomic<uint32_t> armed{0},denialPublished{0};
DRAM_ATTR BleDeniedCall firstDenial{};
DRAM_ATTR pro::trace::HeapSpan heapSpan;
uint32_t stoppedAt=0;
bool settling=false;
struct Guard {
  IRAM_ATTR Guard(){portENTER_CRITICAL_SAFE(&mux);}
  IRAM_ATTR ~Guard(){portEXIT_CRITICAL_SAFE(&mux);}
};
void IRAM_ATTR allocated(void* p,size_t n,uintptr_t caller,Api api){
  if(!armed.load(std::memory_order_relaxed))return;
  Guard guard;tracker.allocation(reinterpret_cast<uintptr_t>(p),n,caller,api);
}
void IRAM_ATTR released(void* p){
  if(!armed.load(std::memory_order_relaxed))return;
  Guard guard;tracker.release(reinterpret_cast<uintptr_t>(p));
}
#if CSP_USB_BENCH_AUTOMATION
void IRAM_ATTR annotated(void* p,uintptr_t caller,Api api){
  if(!armed.load(std::memory_order_relaxed))return;
  Guard guard;tracker.annotate(reinterpret_cast<uintptr_t>(p),caller,api);
}
#endif
Record IRAM_ATTR take(void* p){
  if(!armed.load(std::memory_order_relaxed))return {};
  Guard guard;return tracker.take(reinterpret_cast<uintptr_t>(p));
}
void IRAM_ATTR reallocDone(const Record& old,void* original,void* result,size_t n,uintptr_t caller,Api api){
  if(!armed.load(std::memory_order_relaxed))return;
  Guard guard;
  if(!result&&n){tracker.restore(old);return;} // failure preserves the old object
  if(result!=original||!n)tracker.release(reinterpret_cast<uintptr_t>(original));
  if(result)tracker.allocation(reinterpret_cast<uintptr_t>(result),n,caller,api);
}
bool beforeBlock(walker_heap_into_t h,walker_block_info_t b,void*){
  heapSpan.before(h.start,h.end,reinterpret_cast<uintptr_t>(b.ptr),b.size,b.used);return true;
}
bool afterBlock(walker_heap_into_t h,walker_block_info_t b,void*){
  heapSpan.after(h.start,h.end,reinterpret_cast<uintptr_t>(b.ptr),b.size,b.used);return true;
}
}

void ble_alloc_trace_begin(){
  // Public read-only heap walker: metadata only, never dereference b.ptr.
  // Owner task, before capture; callback does not allocate, log or nest heap APIs.
  heapSpan=pro::trace::HeapSpan{};
  heap_caps_walk(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,beforeBlock,nullptr);
  Guard guard;tracker.begin();settling=false;armed.store(1,std::memory_order_relaxed);
}
void ble_alloc_trace_phase(Phase phase){Guard guard;tracker.phase(phase);}
void ble_alloc_trace_stop(uint32_t now){
  Guard guard;tracker.stop();stoppedAt=now;settling=true;
}
void ble_alloc_trace_poll(uint32_t now){
  if(settling&&uint32_t(now-stoppedAt)>=1000){
    // Do not hold the observer lock while the walker takes per-heap locks.
    heap_caps_walk(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,afterBlock,nullptr);
    heapSpan.captured=true;
    Guard guard;
    tracker.freeze();armed.store(0,std::memory_order_relaxed);settling=false;
  }
}
pro::trace::Summary ble_alloc_trace_summary(){Guard guard;return tracker.summary();}
size_t ble_alloc_trace_page(size_t offset,Record* out,size_t count){Guard guard;return tracker.page(offset,out,count);}
pro::trace::HeapSpan ble_alloc_trace_heap_span(){return heapSpan;} // exported only after Frozen, owner task

void IRAM_ATTR ble_alloc_trace_denial(uint32_t operation,uintptr_t caller){
  uint32_t empty=0;
  if(!denialPublished.compare_exchange_strong(empty,1,std::memory_order_relaxed))return;
  // No buffers, partition names, keys, strings or heap payloads are inspected.
  firstDenial.caller=caller;firstDenial.operation=operation;
  {Guard guard;firstDenial.phase=uint32_t(tracker.current_phase());}
  denialPublished.store(2,std::memory_order_release);
}
BleDeniedCall ble_alloc_trace_denied_call(){
  return denialPublished.load(std::memory_order_acquire)==2?firstDenial:BleDeniedCall{};
}

#define CALLER reinterpret_cast<uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0)))
extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t,size_t);
void* __real_realloc(void*,size_t);
void __real_free(void*);
void* __real_heap_caps_malloc(size_t,uint32_t);
void* __real_heap_caps_calloc(size_t,size_t,uint32_t);
void* __real_heap_caps_realloc(void*,size_t,uint32_t);
void __real_heap_caps_free(void*);
void* __real_heap_caps_aligned_alloc(size_t,size_t,uint32_t);
void* __real_heap_caps_aligned_calloc(size_t,size_t,size_t,uint32_t);
#if CSP_USB_BENCH_AUTOMATION
void* __real_heap_caps_malloc_default(size_t);
void* __real__malloc_r(struct _reent*,size_t);
void* __real__calloc_r(struct _reent*,size_t,size_t);
void* __real__realloc_r(struct _reent*,void*,size_t);
void __real__free_r(struct _reent*,void*);
void* __real_mem_malloc(mem_size_t);
void __real_mem_free(void*);
void* __real_memp_malloc(memp_t);
void __real_memp_free(memp_t,void*);
void* __real_multi_heap_malloc(multi_heap_handle_t,size_t);
void* __real_multi_heap_realloc(multi_heap_handle_t,void*,size_t);
void __real_multi_heap_free(multi_heap_handle_t,void*);
void* __real_multi_heap_aligned_alloc_offs(multi_heap_handle_t,size_t,size_t,size_t);
esp_err_t __real_esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);

void* IRAM_ATTR __wrap_multi_heap_malloc(multi_heap_handle_t h,size_t n){void* p=__real_multi_heap_malloc(h,n);allocated(p,n,CALLER,Api::MultiHeap);return p;}
void* IRAM_ATTR __wrap_multi_heap_realloc(multi_heap_handle_t h,void* p,size_t n){auto old=take(p);void* next=__real_multi_heap_realloc(h,p,n);reallocDone(old,p,next,n,CALLER,Api::MultiHeap);return next;}
void IRAM_ATTR __wrap_multi_heap_free(multi_heap_handle_t h,void* p){released(p);__real_multi_heap_free(h,p);}
void* IRAM_ATTR __wrap_multi_heap_aligned_alloc_offs(multi_heap_handle_t h,size_t n,size_t a,size_t o){void* p=__real_multi_heap_aligned_alloc_offs(h,n,a,o);allocated(p,n,CALLER,Api::MultiHeap);return p;}
esp_err_t IRAM_ATTR __wrap_esp_timer_create(const esp_timer_create_args_t* args,esp_timer_handle_t* out){
  const auto error=__real_esp_timer_create(args,out);
  // Public output handle only; never inspect timer internals, name or argument.
  if(error==ESP_OK&&out&&*out)annotated(*out,CALLER,Api::EspTimer);
  return error;
}

void* IRAM_ATTR __wrap_heap_caps_malloc_default(size_t n){void* p=__real_heap_caps_malloc_default(n);allocated(p,n,CALLER,Api::HeapDefault);return p;}
void* IRAM_ATTR __wrap__malloc_r(struct _reent* r,size_t n){void* p=__real__malloc_r(r,n);allocated(p,n,CALLER,Api::Malloc);return p;}
void* IRAM_ATTR __wrap__calloc_r(struct _reent* r,size_t n,size_t size){void* p=__real__calloc_r(r,n,size);allocated(p,p?n*size:0,CALLER,Api::Calloc);return p;}
void* IRAM_ATTR __wrap__realloc_r(struct _reent* r,void* p,size_t n){auto old=take(p);void* next=__real__realloc_r(r,p,n);reallocDone(old,p,next,n,CALLER,Api::Realloc);return next;}
void IRAM_ATTR __wrap__free_r(struct _reent* r,void* p){released(p);__real__free_r(r,p);}
void* IRAM_ATTR __wrap_mem_malloc(mem_size_t n){void* p=__real_mem_malloc(n);allocated(p,n,CALLER,Api::LwipMem);return p;}
void IRAM_ATTR __wrap_mem_free(void* p){released(p);__real_mem_free(p);}
void* IRAM_ATTR __wrap_memp_malloc(memp_t type){void* p=__real_memp_malloc(type);annotated(p,CALLER,Api::LwipPool);return p;}
void IRAM_ATTR __wrap_memp_free(memp_t type,void* p){released(p);__real_memp_free(type,p);}
#endif

void* IRAM_ATTR __wrap_malloc(size_t n){void* p=__real_malloc(n);allocated(p,n,CALLER,Api::Malloc);return p;}
void* IRAM_ATTR __wrap_calloc(size_t n,size_t size){void* p=__real_calloc(n,size);allocated(p,p?n*size:0,CALLER,Api::Calloc);return p;}
void* IRAM_ATTR __wrap_realloc(void* p,size_t n){auto old=take(p);void* next=__real_realloc(p,n);reallocDone(old,p,next,n,CALLER,Api::Realloc);return next;}
void IRAM_ATTR __wrap_free(void* p){released(p);__real_free(p);}
void* IRAM_ATTR __wrap_heap_caps_malloc(size_t n,uint32_t caps){void* p=__real_heap_caps_malloc(n,caps);allocated(p,n,CALLER,Api::HeapMalloc);return p;}
void* IRAM_ATTR __wrap_heap_caps_calloc(size_t n,size_t size,uint32_t caps){void* p=__real_heap_caps_calloc(n,size,caps);allocated(p,p?n*size:0,CALLER,Api::HeapCalloc);return p;}
void* IRAM_ATTR __wrap_heap_caps_realloc(void* p,size_t n,uint32_t caps){auto old=take(p);void* next=__real_heap_caps_realloc(p,n,caps);reallocDone(old,p,next,n,CALLER,Api::HeapRealloc);return next;}
void IRAM_ATTR __wrap_heap_caps_free(void* p){released(p);__real_heap_caps_free(p);}
void* IRAM_ATTR __wrap_heap_caps_aligned_alloc(size_t alignment,size_t n,uint32_t caps){void* p=__real_heap_caps_aligned_alloc(alignment,n,caps);allocated(p,n,CALLER,Api::Aligned);return p;}
void* IRAM_ATTR __wrap_heap_caps_aligned_calloc(size_t alignment,size_t n,size_t size,uint32_t caps){void* p=__real_heap_caps_aligned_calloc(alignment,n,size,caps);allocated(p,p?n*size:0,CALLER,Api::Aligned);return p;}
}
#undef CALLER
#endif
