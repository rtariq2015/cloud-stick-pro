// Actual pinned NPL bodies with deterministic logical-core interleavings.
// This is not a scheduler/ISR timing or physical Bluetooth test.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
using BaseType_t=int;using TickType_t=uint32_t;using ble_npl_time_t=uint32_t;
constexpr int pdPASS=1,pdTRUE=1,pdFALSE=0,errQUEUE_EMPTY=0;
constexpr TickType_t portMAX_DELAY=UINT32_MAX;
#define BLE_LL_ASSERT(value) assert(value)
#define BLE_HS_DBG_ASSERT_EVAL(value) assert(value)
#define IRAM_ATTR
#define CONFIG_BT_NIMBLE_USE_ESP_TIMER 1
#define OS_MEM_ALLOC 1
#define ESP_OK 0
#define ESP_LOGD(...)
#define ESP_LOGW(...)
#define portNUM_PROCESSORS 2
struct FakeMux {int owner=-1,depth=0;};
#define portMUX_INITIALIZER_UNLOCKED {}
using portMUX_TYPE=FakeMux;
FakeMux ble_port_mutex;FakeMux recursive_sync;
using SemaphoreHandle_t=FakeMux*;
SemaphoreHandle_t npl_eventq_sync=&recursive_sync;
uint8_t hw_critical_state_status[2]{};
int currentCore=1;bool isr=false;unsigned yields=0,critical[2]{},deferred=0;
TickType_t ticks=100;bool forceSendFailure=false,stealPeek=false;
bool injectSnapshot=false,injectRequeue=false,injectDuplicate=false,injecting=false,pending=false;
struct ble_npl_event;struct ble_npl_eventq;
struct EventData {bool queued=false;void(*fn)(ble_npl_event*)=nullptr;void* arg=nullptr;};
#define ble_npl_event_freertos EventData
struct ble_npl_event {void* event=nullptr;};
struct Queue {std::deque<ble_npl_event*> entries;size_t limit=4;};
struct ble_npl_eventq_freertos {Queue* q;};
struct ble_npl_eventq {void* eventq;};
struct ble_npl_callout_freertos {void* handle;ble_npl_event ev;ble_npl_eventq* evq;};
struct ble_npl_callout {void* co;};
struct FakePool{};FakePool ble_freertos_ev_pool,ble_freertos_co_pool;
ble_npl_event* npl_freertos_eventq_get(ble_npl_eventq*,ble_npl_time_t);
void npl_freertos_eventq_put(ble_npl_eventq*,ble_npl_event*);
void npl_freertos_eventq_remove(ble_npl_eventq*,ble_npl_event*);
void npl_freertos_event_deinit(ble_npl_event*);
void npl_freertos_callout_stop(ble_npl_callout*);
void npl_freertos_callout_deinit(ble_npl_callout*);
bool npl_freertos_event_is_queued(ble_npl_event*);
void ble_npl_event_deinit(ble_npl_event* ev){npl_freertos_event_deinit(ev);}
ble_npl_eventq* activeQueue=nullptr;ble_npl_event* foreignEvent=nullptr;
void performForeign();
bool sharedHeld(){return ble_port_mutex.depth||recursive_sync.depth;}
void flushPending(){if(pending&&!sharedHeld()){pending=false;performForeign();}}
void enter(FakeMux* mux){assert(mux->owner==-1||mux->owner==currentCore);mux->owner=currentCore;++mux->depth;++critical[currentCore];}
void leave(FakeMux* mux){assert(mux->owner==currentCore&&mux->depth&&critical[currentCore]);--critical[currentCore];if(!--mux->depth)mux->owner=-1;flushPending();}
#define portENTER_CRITICAL(mux) enter(mux)
#define portEXIT_CRITICAL(mux) leave(mux)
#define portENTER_CRITICAL_ISR(mux) enter(mux)
#define portEXIT_CRITICAL_ISR(mux) leave(mux)
#define portYIELD_FROM_ISR() (++yields)
int xPortInIsrContext(){return isr;}
int xPortGetCoreID(){return currentCore;}
TickType_t xTaskGetTickCount(){return ticks;}
BaseType_t xSemaphoreTakeRecursive(FakeMux* mux,TickType_t wait){
  assert(!critical[currentCore]);
  if(mux->owner!=-1&&mux->owner!=currentCore){assert(wait!=portMAX_DELAY);ticks+=wait;return pdFALSE;}
  mux->owner=currentCore;++mux->depth;return pdTRUE;
}
BaseType_t xSemaphoreGiveRecursive(FakeMux* mux){assert(mux->owner==currentCore&&mux->depth);if(!--mux->depth)mux->owner=-1;flushPending();return pdTRUE;}
void attemptForeign(){
  if(injecting)return;
  if(sharedHeld()){++deferred;pending=true;return;}
  performForeign();
}
unsigned uxQueueMessagesWaiting(Queue* q){auto n=q->entries.size();if(injectSnapshot){injectSnapshot=false;attemptForeign();}return n;}
unsigned uxQueueMessagesWaitingFromISR(Queue* q){return uxQueueMessagesWaiting(q);}
BaseType_t xQueueReceive(Queue* q,ble_npl_event** out,TickType_t wait){
  assert(wait==0||!critical[currentCore]);
  if(q->entries.empty())return errQUEUE_EMPTY;
  *out=q->entries.front();q->entries.pop_front();
  if(injectRequeue){injectRequeue=false;attemptForeign();}
  return pdPASS;
}
BaseType_t xQueueReceiveFromISR(Queue* q,ble_npl_event** out,BaseType_t* woken){*woken=pdFALSE;return xQueueReceive(q,out,0);}
BaseType_t xQueueSendToBack(Queue* q,ble_npl_event* const* ev,TickType_t wait){
  assert(wait==0||!critical[currentCore]);
  if(forceSendFailure||q->entries.size()>=q->limit)return pdFALSE;
  if(injectDuplicate){injectDuplicate=false;attemptForeign();}
  q->entries.push_back(*ev);return pdPASS;
}
BaseType_t xQueueSendToBackFromISR(Queue* q,ble_npl_event* const* ev,BaseType_t* woken){*woken=pdFALSE;return xQueueSendToBack(q,ev,0);}
BaseType_t xQueuePeek(Queue* q,ble_npl_event** ev,TickType_t wait){
  assert(!critical[currentCore]&&!(recursive_sync.depth&&recursive_sync.owner==currentCore));
  if(q->entries.empty()){assert(wait!=portMAX_DELAY);ticks+=wait;return errQUEUE_EMPTY;}
  *ev=q->entries.front();
  if(stealPeek){stealPeek=false;auto saved=currentCore;currentCore=0;npl_freertos_eventq_remove(activeQueue,*ev);currentCore=saved;}
  return pdPASS;
}
bool calloutFreed=false;unsigned eventFrees=0,calloutFrees=0,eventRuns=0,timerStops=0,timerDeletes=0;
int os_memblock_put(FakePool* pool,void* ptr){if(pool==&ble_freertos_ev_pool)++eventFrees;else{assert(pool==&ble_freertos_co_pool);++calloutFrees;calloutFreed=true;}std::free(ptr);return 0;}
int esp_timer_stop(void*){++timerStops;return ESP_OK;}
int esp_timer_delete(void*){++timerDeletes;return ESP_OK;}
using ble_npl_error_t=int;
void npl_freertos_event_run(ble_npl_event* ev){auto* data=static_cast<EventData*>(ev->event);assert(data&&data->fn);data->fn(ev);}
void eventFn(ble_npl_event*){++eventRuns;assert(!critical[currentCore]&&!recursive_sync.depth);}
#include "queue_bodies.h"

void performForeign(){
  assert(!injecting);injecting=true;auto saved=currentCore;bool oldIsr=isr;currentCore=0;isr=false;
  if(foreignEvent)npl_freertos_eventq_put(activeQueue,foreignEvent);
  else while(npl_freertos_eventq_get(activeQueue,0)){}
  currentCore=saved;isr=oldIsr;injecting=false;
}

ble_npl_callout ble_hs_timer{};
bool ble_hs_is_enabled(){return false;}
void ble_npl_callout_stop(ble_npl_callout* co){npl_freertos_callout_stop(co);}
void ble_npl_callout_deinit(ble_npl_callout* co){npl_freertos_callout_deinit(co);}
int ble_npl_callout_reset(ble_npl_callout*,uint32_t){assert(false);return -1;}
#include "timer_body.h"

int main(int argc,char** argv){
  assert(argc==2);std::string mode=argv[1];
  Queue q;ble_npl_eventq_freertos queue{&q};ble_npl_eventq eq{&queue};activeQueue=&eq;
  EventData a{},b{},c{};ble_npl_event first{&a},target{&b},incoming{&c};
  if(mode=="timer-held"||mode=="repeated"){
    const unsigned count=mode=="repeated"?32:1;
    for(unsigned i=0;i<count;++i){
      calloutFreed=false;auto* co=static_cast<ble_npl_callout_freertos*>(std::calloc(1,sizeof(ble_npl_callout_freertos)));
      co->ev.event=std::calloc(1,sizeof(EventData));co->evq=&eq;co->handle=reinterpret_cast<void*>(uintptr_t(1));
      static_cast<EventData*>(co->ev.event)->fn=eventFn;ble_hs_timer.co=co;
      npl_freertos_eventq_put(&eq,&co->ev);auto* held=npl_freertos_eventq_get(&eq,0);assert(held==&co->ev);
      ble_hs_timer_reset(0);
      assert(!calloutFreed); // Old host reset destroys a dequeued in-flight event.
      npl_freertos_event_run(held);npl_freertos_callout_deinit(&ble_hs_timer);
      assert(!ble_hs_timer.co&&calloutFreed&&q.entries.empty());
    }
    assert(eventFrees==count&&calloutFrees==count&&eventRuns==count&&timerDeletes==count);
  }else if(mode=="snapshot"||mode=="snapshot-isr"){
    npl_freertos_eventq_put(&eq,&target);isr=mode=="snapshot-isr";injectSnapshot=true;
    npl_freertos_eventq_remove(&eq,&target);assert(q.entries.empty()&&!b.queued);
    assert(deferred==1);
  }else if(mode=="producer"||mode=="producer-isr"){
    q.limit=2;npl_freertos_eventq_put(&eq,&first);npl_freertos_eventq_put(&eq,&target);
    foreignEvent=&incoming;isr=mode=="producer-isr";injectRequeue=true;
    npl_freertos_eventq_remove(&eq,&target);assert(q.entries.size()==2&&!b.queued&&a.queued&&c.queued&&deferred==1);
    isr=false;assert(npl_freertos_eventq_get(&eq,0)==&first);assert(npl_freertos_eventq_get(&eq,0)==&incoming);
  }else if(mode=="duplicate"){
    foreignEvent=&target;injectDuplicate=true;npl_freertos_eventq_put(&eq,&target);
    assert(q.entries.size()==1&&b.queued&&deferred==1);
  }else if(mode=="genuine-full"){
    q.limit=1;npl_freertos_eventq_put(&eq,&first);npl_freertos_eventq_put(&eq,&target);assert(false);
  }else if(mode=="genuine-requeue"){
    npl_freertos_eventq_put(&eq,&first);npl_freertos_eventq_put(&eq,&target);forceSendFailure=true;
    npl_freertos_eventq_remove(&eq,&target);assert(false);
  }else if(mode=="wrong-queue"){
    b.queued=true;npl_freertos_eventq_remove(&eq,&target);assert(false);
  }else if(mode=="timeout"||mode=="wrap"||mode=="stolen-wake"){
    ticks=mode=="wrap"?UINT32_MAX-4:100;
    if(mode=="stolen-wake"){npl_freertos_eventq_put(&eq,&target);stealPeek=true;}
    auto before=ticks;assert(npl_freertos_eventq_get(&eq,10)==nullptr);assert(TickType_t(ticks-before)==10);
  }else if(mode=="mutex-timeout"){
    npl_freertos_eventq_put(&eq,&target);recursive_sync.owner=0;recursive_sync.depth=1;
    auto before=ticks;assert(npl_freertos_eventq_get(&eq,7)==nullptr);assert(TickType_t(ticks-before)==7);
    recursive_sync={};
  }
#ifdef FIXED_VARIANT
  else if(mode=="critical-depth"){
    auto firstContext=npl_freertos_hw_enter_critical();assert(npl_freertos_hw_is_in_critical()==1);
    auto secondContext=npl_freertos_hw_enter_critical();assert(npl_freertos_hw_is_in_critical()==2);
    currentCore=0;assert(npl_freertos_hw_is_in_critical()==0);currentCore=1;
    npl_freertos_hw_exit_critical(secondContext);assert(npl_freertos_hw_is_in_critical()==1);
    npl_freertos_hw_exit_critical(firstContext);assert(npl_freertos_hw_is_in_critical()==0);
  }
#endif
  else if(mode=="critical-put-remove"){
    portENTER_CRITICAL(&ble_port_mutex);hw_critical_state_status[currentCore]=1;
    npl_freertos_eventq_put(&eq,&first);assert(npl_freertos_event_is_queued(&first));
    assert(npl_freertos_eventq_get(&eq,0)==&first&&!npl_freertos_event_is_queued(&first));
    npl_freertos_eventq_put(&eq,&target);npl_freertos_eventq_remove(&eq,&target);
    assert(!b.queued&&q.entries.empty());
    hw_critical_state_status[currentCore]=0;portEXIT_CRITICAL(&ble_port_mutex);
  }else if(mode=="critical-blocking-get"){
    portENTER_CRITICAL(&ble_port_mutex);hw_critical_state_status[currentCore]=1;
    npl_freertos_eventq_get(&eq,1);assert(false);
  }else if(mode=="deinitialized-query"){
    ble_npl_event empty{};
    assert(!npl_freertos_event_is_queued(&empty));
    isr=true;assert(!npl_freertos_event_is_queued(&empty));isr=false;
  }else if(mode=="nonqueued"){
    npl_freertos_eventq_put(&eq,&first);npl_freertos_eventq_remove(&eq,&target);assert(q.entries.size()==1&&a.queued&&!b.queued);
  }else if(mode=="run-unlocked"){
    a.fn=eventFn;npl_freertos_eventq_put(&eq,&first);auto* ev=npl_freertos_eventq_get(&eq,0);npl_freertos_event_run(ev);assert(eventRuns==1);
  }else assert(false);
  assert(!critical[0]&&!critical[1]&&!recursive_sync.depth&&!ble_port_mutex.depth);
  std::cout<<"PASS "<<mode<<" actual NPL/host timer bodies\n";
}
