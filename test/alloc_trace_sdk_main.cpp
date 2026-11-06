// Host-only SDK integration: interpose the real diagnostic wrappers around
// pinned Security 2/SRP callbacks. These heap_caps stand-ins preserve ownership.
#include "ble_alloc_trace.h"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cstdio>
#include <esp_heap_caps.h>
void heap_caps_walk(uint32_t,heap_caps_walker_cb_t,void*){} // no desktop ESP heap
extern "C" {
void* heap_caps_malloc(size_t n,uint32_t){return std::malloc(n);}
void* heap_caps_calloc(size_t n,size_t size,uint32_t){return std::calloc(n,size);}
void* heap_caps_realloc(void* p,size_t n,uint32_t){return std::realloc(p,n);}
void heap_caps_free(void* p){std::free(p);}
void* heap_caps_aligned_alloc(size_t align,size_t n,uint32_t){void* p=nullptr;return posix_memalign(&p,align,n)?nullptr:p;}
void* heap_caps_aligned_calloc(size_t align,size_t n,size_t size,uint32_t caps){
 if(size&&n>std::numeric_limits<size_t>::max()/size)return nullptr;
 void* p=heap_caps_aligned_alloc(align,n*size,caps);if(p)std::memset(p,0,n*size);return p;
}
}
int csp_test_sdk_main();
int main(){
 ble_alloc_trace_begin();
 const int result=csp_test_sdk_main();
 ble_alloc_trace_stop(0);ble_alloc_trace_poll(1000);
 const auto s=ble_alloc_trace_summary();
 if(s.overflow||!s.allocations){std::fprintf(stderr,"Incomplete SDK allocation-observer coverage\n");return 91;}
 return result;
}
