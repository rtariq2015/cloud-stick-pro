#include "pro_alloc_trace.h"
#include "ble_alloc_trace.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <limits>

// Exercise the real target wrappers with nested, fake heap_caps forwarding.
// They do not replace the desktop allocator or intercept test infrastructure.
#include "../src/ble_alloc_trace.cpp"
static bool failRealloc=false;
static unsigned walks=0;
void heap_caps_walk(uint32_t caps,heap_caps_walker_cb_t cb,void* arg){
 assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));++walks;
 cb({0x3fc90000,0x3fcd0000},{reinterpret_cast<void*>(0x3fc91000),0x30000,false},arg);
}
extern "C" {
void* __real_malloc(size_t n){return __wrap_heap_caps_malloc(n,0);}
void* __real_calloc(size_t n,size_t size){return __wrap_heap_caps_calloc(n,size,0);}
void* __real_realloc(void* p,size_t n){return __wrap_heap_caps_realloc(p,n,0);}
void __real_free(void* p){__wrap_heap_caps_free(p);}
void* __real_heap_caps_malloc(size_t n,uint32_t){return std::malloc(n);}
void* __real_heap_caps_calloc(size_t n,size_t size,uint32_t){return std::calloc(n,size);}
void* __real_heap_caps_realloc(void* p,size_t n,uint32_t){return failRealloc?nullptr:std::realloc(p,n);}
void __real_heap_caps_free(void* p){std::free(p);}
void* __real_heap_caps_aligned_alloc(size_t alignment,size_t n,uint32_t){void* p=nullptr;return posix_memalign(&p,alignment,n)?nullptr:p;}
void* __real_heap_caps_aligned_calloc(size_t alignment,size_t n,size_t size,uint32_t caps){
 if(size&&n>std::numeric_limits<size_t>::max()/size)return nullptr;
 void* p=__real_heap_caps_aligned_alloc(alignment,n*size,caps);if(p)std::memset(p,0,n*size);return p;
}
#if CSP_USB_BENCH_AUTOMATION
void* __real_heap_caps_malloc_default(size_t n){return __wrap_heap_caps_malloc(n,0);}
void* __real__malloc_r(struct _reent*,size_t n){return __wrap_heap_caps_malloc_default(n);}
void* __real__calloc_r(struct _reent*,size_t n,size_t size){return __wrap_heap_caps_calloc(n,size,0);}
void* __real__realloc_r(struct _reent*,void* p,size_t n){return __wrap_heap_caps_realloc(p,n,0);}
void __real__free_r(struct _reent*,void* p){__wrap_heap_caps_free(p);}
void* __real_mem_malloc(mem_size_t n){return __wrap__malloc_r(nullptr,n);}
void __real_mem_free(void* p){__wrap__free_r(nullptr,p);}
void* __real_memp_malloc(memp_t){return __wrap_mem_malloc(28);}
void __real_memp_free(memp_t,void* p){__wrap_mem_free(p);}
void* __real_multi_heap_malloc(multi_heap_handle_t,size_t n){return std::malloc(n);}
void* __real_multi_heap_realloc(multi_heap_handle_t,void* p,size_t n){return failRealloc?nullptr:std::realloc(p,n);}
void __real_multi_heap_free(multi_heap_handle_t,void* p){std::free(p);}
void* __real_multi_heap_aligned_alloc_offs(multi_heap_handle_t,size_t n,size_t a,size_t offset){assert(offset==0);void* p=nullptr;return posix_memalign(&p,a,n)?nullptr:p;}
esp_err_t __real_esp_timer_create(const esp_timer_create_args_t* args,esp_timer_handle_t* out){
 if(!args||!args->callback||!out)return -1;
 *out=__wrap_heap_caps_calloc(1,32,0);return *out?ESP_OK:-2;
}
#endif
}

int main(){
 using namespace pro::trace;
 HeapSpan span;
 span.before(100,10000,1000,8000,false);span.before(100,10000,200,20,true);
 span.after(100,10000,500,20,true); // outside baseline free region
 span.after(100,10000,1000,500,false);span.after(100,10000,1600,16,true);
 span.after(100,10000,1624,7376,false);
 assert(span.before_bytes==8000&&span.after_bytes==7376&&span.barriers==1);
 assert(span.retained[0].address==1600&&span.retained[0].bytes==16);
 for(unsigned i=0;i<20;++i)span.after(100,10000,2000+i*20,16,true);
 assert(span.barriers==16&&span.overflow==5);
 HeapSpan coalesced;coalesced.before(0,10000,2000,2000,false);
 coalesced.after(0,10000,1000,4000,false);assert(coalesced.after_start==1000&&coalesced.after_bytes==4000);
 coalesced.after(0,10000,1000,2000,true);assert(coalesced.barriers==1&&coalesced.retained[0].address==1000);
 void* cold=__wrap_malloc(12);assert(cold);__wrap_free(cold);
 assert(ble_alloc_trace_summary().snapshot==0);
 // Core: cold/unarmed calls, null allocations, nested calls, bounded capacity.
 Tracker<2> core;
 core.allocation(1,20,30,Api::Malloc);assert(core.summary().allocations==0);
 core.begin();core.allocation(0,1,1,Api::Malloc);assert(core.summary().failed==1);
 core.allocation(1,20,30,Api::HeapMalloc);core.allocation(1,20,40,Api::Malloc);
 core.allocation(2,30,50,Api::Calloc);core.allocation(3,30,60,Api::Malloc);
 assert(core.summary().overflow==1&&core.summary().duplicates==1);
 core.release(2);core.allocation(3,40,60,Api::Malloc);
 core.phase(Phase::Authenticated);core.stop();assert(core.summary().at_stop==2);
 core.allocation(4,80,90,Api::Malloc);assert(core.summary().at_stop==2);
 core.release(1);core.freeze();auto s=core.summary();
 assert(s.live==1&&s.released==1&&s.live_bytes==40);
 Record rows[12];assert(core.page(0,rows,12)==2&&core.page(2,rows,12)==0);
 assert(rows[0].caller==40&&!rows[0].live&&rows[1].address==3&&rows[1].live);
 core.release(3);assert(core.summary().live==1); // immutable after freeze
 core.begin();assert(core.summary().snapshot==2&&core.summary().overflow==0);

 // Outer pool attribution preserves a nested allocator's measured byte count.
 core.annotate(0,99,Api::LwipPool);assert(core.summary().allocations==0);
 core.allocation(1,28,30,Api::HeapDefault);
 core.annotate(1,99,Api::LwipPool);core.annotate(2,100,Api::LwipPool);
 core.annotate(3,101,Api::LwipPool);assert(core.summary().overflow==1);
 core.stop();core.freeze();assert(core.page(0,rows,12)==2);
 assert(rows[0].bytes==28&&rows[0].caller==99&&rows[0].api==Api::LwipPool);
 assert(rows[1].bytes==0&&rows[1].caller==100);
 core.annotate(1,999,Api::Malloc);assert(core.page(0,rows,12)==2&&rows[0].caller==99);

 // Timeouts can free/reallocate during the final one-second snapshot delay.
 // Include the replacement, even when it reuses a released export slot.
 Tracker<2> late;late.begin();late.allocation(10,16,100,Api::Malloc);
 late.stop();late.release(10);late.allocation(20,16,200,Api::LwipPool);
 late.stop();assert(late.summary().at_stop==1); // repeated stop is idempotent
 auto saved=late.take(20);assert(late.summary().at_stop==0);
 late.restore(saved);late.annotate(20,201,Api::LwipPool);
 late.freeze();assert(late.page(0,rows,12)==1&&rows[0].address==20&&rows[0].caller==201);
 assert(late.summary().live==1&&late.summary().live_bytes==16);

#if CSP_USB_BENCH_AUTOMATION
 ble_alloc_trace_begin();
 auto* pool=static_cast<unsigned char*>(__wrap_memp_malloc(0));assert(pool);
 std::memset(pool,0x3c,28);
 auto* network=static_cast<unsigned char*>(__wrap__calloc_r(nullptr,4,8));assert(network);
 for(size_t i=0;i<32;++i)assert(network[i]==0);
 failRealloc=true;assert(!__wrap__realloc_r(nullptr,network,64));failRealloc=false;
 network=static_cast<unsigned char*>(__wrap__realloc_r(nullptr,network,64));assert(network);
 for(size_t i=0;i<32;++i)assert(network[i]==0);
 __wrap__free_r(nullptr,network);
 ble_alloc_trace_stop(0);ble_alloc_trace_poll(1000);
 assert(ble_alloc_trace_page(0,rows,12)==1&&rows[0].bytes==28&&rows[0].api==Api::LwipPool);
 for(size_t i=0;i<28;++i)assert(pool[i]==0x3c);
 __wrap_memp_free(0,pool);
 ble_alloc_trace_begin();
 void* raw=__wrap_multi_heap_malloc(nullptr,8);assert(raw);
 failRealloc=true;assert(!__wrap_multi_heap_realloc(nullptr,raw,16));failRealloc=false;
 raw=__wrap_multi_heap_realloc(nullptr,raw,16);assert(raw);
 void* alignedRaw=__wrap_multi_heap_aligned_alloc_offs(nullptr,32,16,0);assert(alignedRaw);
 __wrap_multi_heap_free(nullptr,alignedRaw);
 esp_timer_handle_t handle=nullptr;assert(__wrap_esp_timer_create(nullptr,&handle)!=ESP_OK&&!handle);
 esp_timer_create_args_t timerArgs{[](void*){},nullptr};
 assert(__wrap_esp_timer_create(&timerArgs,&handle)==ESP_OK&&handle);
 ble_alloc_trace_stop(0);ble_alloc_trace_poll(1000);
 assert(ble_alloc_trace_page(0,rows,12)==2);
 bool foundTimer=false;for(size_t i=0;i<2;++i)if(rows[i].api==Api::EspTimer){assert(rows[i].bytes==32);foundTimer=true;}
 assert(foundTimer);__wrap_free(handle);__wrap_multi_heap_free(nullptr,raw);
 walks=0;
#endif

 // Actual wrappers: all forms preserve return values and caller-owned bytes.
 ble_alloc_trace_begin();ble_alloc_trace_phase(Phase::Ready);
 auto* a=static_cast<unsigned char*>(__wrap_malloc(64));assert(a);
 std::memset(a,0x5a,64);
 auto* b=static_cast<unsigned char*>(__wrap_calloc(4,16));assert(b);
 for(size_t i=0;i<64;++i)assert(b[i]==0);
 failRealloc=true;assert(!__wrap_realloc(a,128));failRealloc=false;
 for(size_t i=0;i<64;++i)assert(a[i]==0x5a);
 a=static_cast<unsigned char*>(__wrap_realloc(a,128));assert(a);
 for(size_t i=0;i<64;++i)assert(a[i]==0x5a);
 auto* aligned=static_cast<unsigned char*>(__wrap_heap_caps_aligned_calloc(32,4,16,0));
 assert(aligned&&reinterpret_cast<uintptr_t>(aligned)%32==0);
 for(size_t i=0;i<64;++i)assert(aligned[i]==0);
 __wrap_free(b);
 ble_alloc_trace_phase(Phase::Authenticated);
 ble_alloc_trace_denial(3,0x42012340);ble_alloc_trace_denial(4,0x42099990);
 auto denial=ble_alloc_trace_denied_call();assert(denial.caller==0x42012340&&denial.operation==3&&denial.phase==uint32_t(Phase::Authenticated));
 ble_alloc_trace_stop(1000);assert(ble_alloc_trace_summary().at_stop==2);
 __wrap_free(aligned);ble_alloc_trace_poll(1999);assert(ble_alloc_trace_summary().phase==Phase::Settling);
 ble_alloc_trace_poll(2000);s=ble_alloc_trace_summary();
 assert(s.phase==Phase::Frozen&&s.live==1&&s.released==1&&s.live_bytes==128&&s.overflow==0);
 assert(ble_alloc_trace_page(0,rows,12)==2);__wrap_free(a);

 // No allocator payload reads: tracker accepts a synthetic inaccessible value.
 core.begin();core.allocation(0x1,123,0x42010000,Api::Malloc);core.stop();core.freeze();assert(core.summary().live_bytes==123);
 // Wrap-safe settle timing and realloc(ptr,0) release semantics.
 ble_alloc_trace_begin();void* z=__wrap_malloc(32);assert(z);assert(!__wrap_realloc(z,0));
 ble_alloc_trace_stop(UINT32_MAX-500);ble_alloc_trace_poll(499);assert(ble_alloc_trace_summary().phase==Phase::Frozen);
 assert(ble_alloc_trace_summary().at_stop==0);

 ble_alloc_trace_begin();void* timer=__wrap_malloc(16);assert(timer);
 ble_alloc_trace_stop(1000);__wrap_free(timer);timer=__wrap_malloc(16);assert(timer);
 ble_alloc_trace_poll(2000);assert(ble_alloc_trace_page(0,rows,12)==1);
 assert(rows[0].address==reinterpret_cast<uintptr_t>(timer)&&rows[0].live);
 __wrap_free(timer);walks-=2;

 // Actual synchronization under concurrent allocation/free, no target timing claim.
 ble_alloc_trace_begin();std::vector<std::thread> threads;
 for(unsigned t=0;t<8;++t)threads.emplace_back([]{for(unsigned n=0;n<2000;++n){void* p=__wrap_malloc(32+n%64);assert(p);__wrap_free(p);}});
 for(auto& t:threads)t.join();
 ble_alloc_trace_stop(0);ble_alloc_trace_poll(1000);
 s=ble_alloc_trace_summary();assert(s.at_stop==0&&s.live==0&&s.overflow==0);
 const auto captured=ble_alloc_trace_heap_span();
 assert(walks==6&&captured.captured&&captured.before_bytes==0x30000&&captured.after_bytes==0x30000);
 assert(captured.heap_start==0x3fc90000&&captured.barriers==0&&captured.overflow==0);
 ble_alloc_trace_poll(1001);assert(walks==6); // frozen metadata is never rescanned
 std::cout<<"PASS: allocation ownership, nested wrappers, failed/moved/zero realloc, aligned calloc, immutable pagination, delayed free, first denial, timer wrap, 16000 concurrent allocations\n";
}
