// Compiles the production adapter into this translation unit.
// SDK/radio/clock stubs deliberately expose allowed task interleavings.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include "adapter_stubs/common.h"
std::atomic<uint32_t> simulatedNow{100000};
std::atomic<bool> blockCallbackMillis{false},callbackAtMillis{false},releaseCallback{false};
thread_local bool isCallbackThread=false;
std::function<void()> duringMillis;
int simulatedStopCalls=0;
bool simulatedStopOk=true;
static int session_ok(void*,uint32_t){return ESP_OK;}
static int handshake_ok(void*,const void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*){return ESP_OK;}
static int frame_ok(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*){return ESP_OK;}
const protocomm_security_t protocomm_security2={2,1,nullptr,nullptr,session_ok,session_ok,handshake_ok,frame_ok,frame_ok};
#include "../../src/ble_management.cpp"
uint32_t millis(){
  if(isCallbackThread && blockCallbackMillis.exchange(false)) {
    callbackAtMillis.store(true);
    while(!releaseCallback.load())std::this_thread::yield();
  }
  uint32_t snapshot=simulatedNow.load();
  if(duringMillis){
    auto callback=std::move(duringMillis);duringMillis=nullptr;
    simulatedNow.store(snapshot+1);callback();return snapshot;
  }
  return snapshot;
}
int main(int argc,char** argv){
  assert(argc==2);
  std::string scenario=argv[1];
  assert(ble_management_begin());
  if(scenario=="stale-connect"){
    blockCallbackMillis.store(true);
    std::thread callback([]{isCallbackThread=true;assert(newSession(nullptr,7)==ESP_OK);});
    while(!callbackAtMillis.load())std::this_thread::yield();
    assert(!connected.load() && connectedAt.load()==0);
    ble_management_poll();
    assert(simulatedStopCalls==0);
    releaseCallback.store(true);callback.join();
    std::puts("PASS no false stop: connection remains unpublished while its callback timestamp is pending");
  } else if(scenario=="future-info"){
    assert(newSession(nullptr,7)==ESP_OK);
    uint8_t* output=nullptr;ssize_t outputSize=0;
    const char* first="{\"v\":1,\"id\":1,\"op\":\"info\"}";
    assert(info(7,reinterpret_cast<const uint8_t*>(first),std::char_traits<char>::length(first),&output,&outputSize,nullptr)==ESP_OK);
    std::free(output);output=nullptr;
    simulatedNow.store(100010);
    duringMillis=[&] {
      const char* next="{\"v\":1,\"id\":2,\"op\":\"info\"}";
      assert(info(7,reinterpret_cast<const uint8_t*>(next),std::char_traits<char>::length(next),&output,&outputSize,nullptr)==ESP_OK);
    };
    ble_management_poll();
    assert(authenticated.load() && lastActivity.load()==100011 && simulatedStopCalls==0);
    std::free(output);
    std::puts("PASS no false stop: refresh published lastActivity=100011 after poll sampled now=100010");
  } else if(scenario=="future-connect"){
    // Model a concurrent connection callback between taking poll's 'now' sample
    // and evaluating its predicates, crossing a one-millisecond clock tick.
    duringMillis=[] { assert(newSession(nullptr,7)==ESP_OK); };
    ble_management_poll();
    assert(connectedAt.load()==100001 && simulatedStopCalls==0);
    std::puts("PASS no false stop: callback published connectedAt=100001 after poll sampled now=100000");
  } else if(scenario=="timeout-wrap"){
    // Begin and connect near millis wrap. A real elapsed handshake interval
    // still expires once; timestamp subtraction alone must not ignore wrap.
    assert(ble_management_stop());simulatedStopCalls=0;
    simulatedNow.store(0xfffffff0U);assert(ble_management_begin());
    assert(newSession(nullptr,7)==ESP_OK);
    simulatedNow.store(0xfffffff0U+HandshakeMs-1);ble_management_poll();
    assert(simulatedStopCalls==0);
    simulatedNow.store(0xfffffff0U+HandshakeMs);ble_management_poll();
    assert(simulatedStopCalls==1 && !ble_management_active());
    std::puts("PASS actual wrapped handshake timeout triggers at exact bound");
  } else if(scenario=="timeout-new-generation"){
    assert(newSession(nullptr,7)==ESP_OK);
    simulatedNow.store(130000);
    // An old connection is expired in the poll snapshot. Its callback closes
    // and a new session is published before that terminal decision is applied.
    duringMillis=[] {
      assert(closeSession(nullptr,7)==ESP_OK);
      assert(newSession(nullptr,8)==ESP_OK);
    };
    ble_management_poll();
    assert(session.load()==8 && connectedAt.load()==130001 && simulatedStopCalls==0);
    std::puts("PASS old timeout cannot terminate a replacement session");
  } else if(scenario=="timeout-refreshed-generation"){
    assert(newSession(nullptr,7)==ESP_OK);
    uint8_t* output=nullptr;ssize_t outputSize=0;
    const char* first="{\"v\":1,\"id\":1,\"op\":\"info\"}";
    assert(info(7,reinterpret_cast<const uint8_t*>(first),std::strlen(first),&output,&outputSize,nullptr)==ESP_OK);
    std::free(output);output=nullptr;
    simulatedNow.store(160000);
    duringMillis=[&] {
      const char* next="{\"v\":1,\"id\":2,\"op\":\"info\"}";
      assert(info(7,reinterpret_cast<const uint8_t*>(next),std::strlen(next),&output,&outputSize,nullptr)==ESP_OK);
    };
    ble_management_poll();std::free(output);
    assert(lastActivity.load()==160001 && simulatedStopCalls==0);
    std::puts("PASS old idle decision cannot terminate refreshed activity");
  } else if(scenario=="absolute-window"){
    assert(newSession(nullptr,7)==ESP_OK);
    simulatedNow.store(220000);
    uint8_t* output=nullptr;ssize_t outputSize=0;
    duringMillis=[&] {
      const char* first="{\"v\":1,\"id\":1,\"op\":\"info\"}";
      assert(info(7,reinterpret_cast<const uint8_t*>(first),std::strlen(first),&output,&outputSize,nullptr)==ESP_OK);
    };
    ble_management_poll();std::free(output);
    assert(simulatedStopCalls==1 && !ble_management_active());
    std::puts("PASS absolute physical window expires despite concurrent activity");
  } else return 2;
  assert(ble_management_stop());
  return 0;
}
