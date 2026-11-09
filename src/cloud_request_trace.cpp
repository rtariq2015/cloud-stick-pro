#include "cloud_request_trace.h"
#if CSP_ENABLE_CLOUD_REQUEST_TRACE
#include <Arduino.h>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <mutex>
#include <thread>
#endif
namespace {
#ifdef ARDUINO
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
struct Lock {Lock(){portENTER_CRITICAL(&mux);}~Lock(){portEXIT_CRITICAL(&mux);}};
#else
std::mutex mux;
struct Lock {std::lock_guard<std::mutex> guard{mux};};
#endif
CloudRequestSnapshot state;
uint32_t currentStarted=0;
#ifdef ARDUINO
using Owner=TaskHandle_t;
Owner currentOwner(){return xTaskGetCurrentTaskHandle();}
#else
using Owner=std::thread::id;
Owner currentOwner(){return std::this_thread::get_id();}
#endif
Owner requestOwner{};
}
CloudRequestTrace::CloudRequestTrace(CloudRequestOp op){
  started_=millis();Lock lock;
  // There is one admitted backup/coordinator request at a time. Never replace
  // its observation if a nested or unrelated scope is accidentally attempted.
  if(state.active)return;
  if(state.attempts!=UINT32_MAX)++state.attempts;
  event_.id=state.attempts;event_.operation=op;
  state.current=event_;state.active=true;currentStarted=started_;requestOwner=currentOwner();
}
pro::Result CloudRequestTrace::finish(pro::Result result){
  const uint32_t now=millis();
  if(finished_||!event_.id)return result;
  finished_=true;event_.elapsed_ms=now-started_;event_.result_code=result.code;
  Lock lock;
  if(state.active&&state.current.id==event_.id){
    event_.tls_error=state.current.tls_error;
    state.last=event_;state.current=event_;state.active=false;
    requestOwner=Owner{};
    // Positive statuses such as a normal staging 404 fallback are not labelled
    // negative transport errors. A request error is not a file-outcome claim.
    if(!result.ok&&result.code<0)state.last_negative=event_;
  }
  return result;
}
CloudRequestTrace::~CloudRequestTrace(){if(!finished_)finish({false,pro::SnapshotChanged});}
#if CSP_ENABLE_CLOUD_TLS_TRACE
void cloud_request_tls_error(int32_t code){
  if(code>=0)return;
  Lock lock;
  if(state.active&&requestOwner==currentOwner())state.current.tls_error=code;
}
#endif
CloudRequestSnapshot cloud_request_snapshot(){
  const uint32_t now=millis();Lock lock;auto copy=state;copy.enabled=true;
  if(copy.active)copy.current.elapsed_ms=now-currentStarted;
  return copy;
}
#endif
