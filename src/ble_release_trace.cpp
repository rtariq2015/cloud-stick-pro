// SPDX-License-Identifier: MIT
#include "ble_release_trace.h"
#if CSP_BLE_RELEASE_DIAGNOSTICS
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <atomic>
#include <limits>
namespace {
DRAM_ATTR portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR pro::trace::Tracker<128> tracker;
DRAM_ATTR std::atomic<bool> armed{false};
pro::trace::HeapSpan span;
struct Lock {IRAM_ATTR Lock(){portENTER_CRITICAL_SAFE(&mux);}IRAM_ATTR ~Lock(){portEXIT_CRITICAL_SAFE(&mux);}};
void IRAM_ATTR added(void* p,size_t n,uintptr_t caller){
  if(!armed.load(std::memory_order_relaxed)||n>256)return;
  Lock lock;tracker.allocation(reinterpret_cast<uintptr_t>(p),n,caller,pro::trace::Api::Malloc);
}
void IRAM_ATTR freed(void* p){if(armed.load(std::memory_order_relaxed)){Lock lock;tracker.release(reinterpret_cast<uintptr_t>(p));}}
bool before(walker_heap_into_t h,walker_block_info_t b,void*){span.before(h.start,h.end,reinterpret_cast<uintptr_t>(b.ptr),b.size,b.used);return true;}
bool after(walker_heap_into_t h,walker_block_info_t b,void*){span.after(h.start,h.end,reinterpret_cast<uintptr_t>(b.ptr),b.size,b.used);return true;}
}
void ble_release_trace_begin(){
  armed.store(false);span={};heap_caps_walk(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,before,nullptr);
  Lock lock;tracker.begin();armed.store(true);
}
void ble_release_trace_finish(){
  // Metadata only: no block contents, user values, SDK frees or lifecycle action.
  armed.store(false);heap_caps_walk(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,after,nullptr);span.captured=true;
  Lock lock;tracker.stop();tracker.freeze();
}
pro::trace::HeapSpan ble_release_trace_span(){return span;}
pro::trace::Summary ble_release_trace_summary(){Lock lock;return tracker.summary();}
size_t ble_release_trace_page(size_t offset,pro::trace::Record* out,size_t size){Lock lock;return tracker.page(offset,out,size);}
#define CSP_RELEASE_CALLER reinterpret_cast<uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0)))
extern "C" {
void* __real_malloc(size_t);void* __real_calloc(size_t,size_t);void __real_free(void*);
void* __real_heap_caps_malloc(size_t,uint32_t);void* __real_heap_caps_calloc(size_t,size_t,uint32_t);void __real_heap_caps_free(void*);
void* IRAM_ATTR __wrap_malloc(size_t n){void* p=__real_malloc(n);added(p,n,CSP_RELEASE_CALLER);return p;}
void* IRAM_ATTR __wrap_calloc(size_t n,size_t s){void* p=__real_calloc(n,s);if(!s||n<=SIZE_MAX/s)added(p,n*s,CSP_RELEASE_CALLER);return p;}
void IRAM_ATTR __wrap_free(void* p){freed(p);__real_free(p);}
void* IRAM_ATTR __wrap_heap_caps_malloc(size_t n,uint32_t caps){void* p=__real_heap_caps_malloc(n,caps);added(p,n,CSP_RELEASE_CALLER);return p;}
void* IRAM_ATTR __wrap_heap_caps_calloc(size_t n,size_t s,uint32_t caps){void* p=__real_heap_caps_calloc(n,s,caps);if(!s||n<=SIZE_MAX/s)added(p,n*s,CSP_RELEASE_CALLER);return p;}
void IRAM_ATTR __wrap_heap_caps_free(void* p){freed(p);__real_heap_caps_free(p);}
}
#endif
