// Actual patched pool lifecycle, with tracked resource adapters only.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <iostream>
#define MYNEWT_VAL(x) MYNEWT_##x
#define MYNEWT_BLE_STATIC_TO_DYNAMIC 1
#define MYNEWT_MP_RUNTIME_ALLOC 0
#define SOC_ESP_NIMBLE_CONTROLLER 1
#define CONFIG_BT_CONTROLLER_ENABLED 1
#define BLE_LL_ASSERT(x) assert(x)
#define OS_MEMPOOL_SIZE(n,s) ((n)*(s))
constexpr int BLE_TOTAL_EV_COUNT=2,BLE_TOTAL_EVQ_COUNT=2,BLE_TOTAL_CO_COUNT=2,BLE_TOTAL_SEM_COUNT=2,BLE_TOTAL_MUTEX_COUNT=2;
struct ble_npl_event_freertos {int placeholder;};
struct ble_npl_eventq_freertos {int placeholder;};
struct ble_npl_callout_freertos {int placeholder;};
struct ble_npl_sem_freertos {int placeholder;};
struct ble_npl_mutex_freertos {int placeholder;};
using os_membuf_t=uint32_t;
using SemaphoreHandle_t=void*;
struct Pool{};
Pool ble_freertos_ev_pool,ble_freertos_evq_pool,ble_freertos_co_pool,ble_freertos_sem_pool,ble_freertos_mutex_pool;
void *ble_freertos_ev_buf=nullptr,*ble_freertos_evq_buf=nullptr,*ble_freertos_co_buf=nullptr,*ble_freertos_sem_buf=nullptr,*ble_freertos_mutex_buf=nullptr,*ble_freertos_ctx=nullptr;
SemaphoreHandle_t npl_eventq_sync=nullptr;
std::set<void*> allocated;
bool failMutex=false;unsigned creations=0,deletions=0,poolInits=0;
void* allocate(size_t size){void* p=std::calloc(1,size);assert(p);assert(allocated.insert(p).second);return p;}
void nimble_platform_mem_free(void* p){assert(allocated.erase(p)==1);std::free(p);}
void* nimble_platform_mem_calloc(size_t n,size_t size){return allocate(n*size);}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){++creations;return failMutex?nullptr:allocate(16);}
void vSemaphoreDelete(SemaphoreHandle_t p){++deletions;nimble_platform_mem_free(p);}
int ble_freertos_ensure_ctx(){if(!ble_freertos_ctx)ble_freertos_ctx=allocate(16);return 0;}
int os_mempool_init(Pool*,int,size_t,void*,const char*){++poolInits;return 0;}
void os_mempool_unregister(Pool*){}
#include "pool_bodies.h"
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];
 if(mode=="mutex-allocation-failure"){
  failMutex=true;assert(npl_freertos_mempool_init()==-1);
  assert(!npl_eventq_sync&&!ble_freertos_ctx&&poolInits==0&&allocated.empty());
  npl_freertos_mempool_deinit();assert(deletions==0);
 }else if(mode=="repeated-cleanup"){
  for(unsigned i=0;i<64;++i){assert(npl_freertos_mempool_init()==0);assert(npl_eventq_sync&&allocated.size()==7);npl_freertos_mempool_deinit();assert(allocated.empty()&&!npl_eventq_sync);npl_freertos_mempool_deinit();}
  assert(creations==64&&deletions==64&&poolInits==320);
 }else assert(false);
 assert(allocated.empty());std::cout<<"PASS "<<mode<<" actual NPL pool functions\n";
}
