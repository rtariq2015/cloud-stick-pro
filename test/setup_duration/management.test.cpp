// Production adapter; synthetic SDK/heap/clock only, no radio/device evidence.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#define heap_caps_get_free_size fixture_baseline_free
#define heap_caps_get_largest_free_block fixture_baseline_largest
#include "../milestone1_corrections/adapter_stubs/common.h"
#undef heap_caps_get_free_size
#undef heap_caps_get_largest_free_block
size_t heapFree=500000,heapLargest=250000;
size_t heap_caps_get_free_size(int){return heapFree;}
size_t heap_caps_get_largest_free_block(int){return heapLargest;}
uint32_t nowValue=100000;
std::function<void()> duringMillis;
int simulatedStopCalls=0;
bool simulatedStopOk=true,frameValid=true;
int sessionCalls=0,frameCalls=0;
static int session_ok(void*,uint32_t){++sessionCalls;return ESP_OK;}
static int handshake_ok(void*,const void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*){return ESP_OK;}
static int frame_ok(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*){++frameCalls;return frameValid?ESP_OK:ESP_ERR_INVALID_ARG;}
const protocomm_security_t protocomm_security2={2,1,nullptr,nullptr,session_ok,session_ok,handshake_ok,frame_ok,frame_ok};
#include "../../src/ble_management.cpp"
uint32_t millis(){const auto sample=nowValue;if(duringMillis){auto fn=std::move(duringMillis);duringMillis=nullptr;fn();}return sample;}
int readInfo(uint32_t id=1){
 const auto text=std::string("{\"v\":1,\"id\":")+std::to_string(id)+",\"op\":\"info\"}";
 uint8_t* out=nullptr;ssize_t length=0;
 const int result=info(7,reinterpret_cast<const uint8_t*>(text.data()),ssize_t(text.size()),&out,&length,nullptr);
 std::free(out);return result;
}
void connectAt(uint32_t time){nowValue=time;assert(newSession(nullptr,7)==ESP_OK);}
void authenticateAt(uint32_t time){nowValue=time;assert(readInfo()==ESP_OK);assert(ble_management_authenticated());}
int main(int argc,char** argv){
 assert(argc==2);assert(pro::setup_session::Guided&&IdleMs==600000&&WindowMs==120000&&HandshakeMs==30000);
 const std::string which=argv[1];assert(ble_management_begin());
 if(which=="unauthenticated-window"){
  nowValue=219999;ble_management_poll();assert(simulatedStopCalls==0);
  nowValue=220000;ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active()&&ble_management_last_error()==ESP_ERR_TIMEOUT);
 }else if(which=="late-session-admission"){
  nowValue=220000;assert(newSession(nullptr,7)!=ESP_OK);assert(sessionCalls==0&&stopRequested.load());ble_management_poll();assert(!ble_management_active());
 }else if(which=="handshake-exact"){
  connectAt(100005);nowValue=130004;ble_management_poll();assert(simulatedStopCalls==0);
  nowValue=130005;ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active());
 }else if(which=="late-handshake-admission"){
  connectAt(100005);nowValue=130005;uint8_t* out=nullptr;ssize_t n=0;const uint8_t bytes[]{1};
  assert(secureHandshake(nullptr,nullptr,7,bytes,1,&out,&n,nullptr)!=ESP_OK);assert(stopRequested.load()&&ble_management_last_error()==ESP_ERR_TIMEOUT);
 }else if(which=="authenticated-past-window"){
  connectAt(100000);authenticateAt(100010);nowValue=220001;assert(readInfo(2)==ESP_OK);ble_management_poll();assert(simulatedStopCalls==0&&lastActivity.load()==220001);
 }else if(which=="idle-exact"){
  connectAt(100000);authenticateAt(100010);nowValue=700009;ble_management_poll();assert(simulatedStopCalls==0);
  nowValue=700010;ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active()&&ble_management_last_error()==ESP_ERR_TIMEOUT);
 }else if(which=="valid-activity-refresh"){
  connectAt(100000);authenticateAt(100010);nowValue=690000;assert(readInfo(2)==ESP_OK);nowValue=1000000;ble_management_poll();assert(simulatedStopCalls==0&&lastActivity.load()==690000);
 }else if(which=="late-activity-denied"){
  connectAt(100000);authenticateAt(100010);nowValue=700010;assert(readInfo(2)!=ESP_OK);assert(lastActivity.load()==100010&&stopRequested.load()&&ble_management_last_error()==ESP_ERR_TIMEOUT);
 }else if(which=="stale-poll-refresh"){
  connectAt(100000);authenticateAt(100010);nowValue=700010;duringMillis=[] {nowValue=700009;assert(readInfo(2)==ESP_OK);};ble_management_poll();
  assert(simulatedStopCalls==0&&lastActivity.load()==700009);
 }else if(which=="stale-poll-authentication"){
  connectAt(219980);nowValue=220000;duringMillis=[] {nowValue=219999;assert(readInfo()==ESP_OK);};ble_management_poll();assert(simulatedStopCalls==0&&ble_management_authenticated());
 }else if(which=="late-poll-authentication-denied"){
  connectAt(219980);nowValue=220000;duringMillis=[] {nowValue=220001;assert(readInfo()!=ESP_OK);};ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active());
 }else if(which=="disconnect-prompt"){
  connectAt(100000);authenticateAt(100010);nowValue=100020;assert(closeSession(nullptr,7)==ESP_OK);
  assert(stopRequested.load()&&!ble_management_authenticated()&&ble_management_last_error()==0);ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active());
 }else if(which=="disconnect-terminal"){
  connectAt(100000);authenticateAt(100010);assert(closeSession(nullptr,7)==ESP_OK);const int count=sessionCalls;
  assert(newSession(nullptr,8)!=ESP_OK&&sessionCalls==count);assert(!ble_management_authenticated()&&ble_management_last_error()==0);
 }else if(which=="unrelated-close"){
  connectAt(100000);authenticateAt(100010);assert(closeSession(nullptr,8)==ESP_OK);assert(!stopRequested.load()&&ble_management_authenticated());
 }else if(which=="invalid-frame-terminal"){
  connectAt(100000);authenticateAt(100010);nowValue=220001;uint8_t* out=nullptr;ssize_t n=0;uint8_t bad[17]{};frameValid=false;
  assert(decrypt(nullptr,7,bad,sizeof bad,&out,&n)!=ESP_OK);assert(frameCalls==1&&stopRequested.load());assert(readInfo(2)!=ESP_OK);assert(lastActivity.load()==100010);
 }else if(which=="low-memory-preserved"){
  connectAt(100000);authenticateAt(100010);nowValue=220001;heapFree=32767;ble_management_poll();assert(!ble_management_active()&&ble_management_last_error()==ESP_ERR_NO_MEM);
 }else if(which=="idle-wrap"){
  assert(ble_management_stop());simulatedStopCalls=0;nowValue=0xfffffff0u;assert(ble_management_begin());connectAt(0xfffffff1u);authenticateAt(0xfffffff2u);
  nowValue=uint32_t(0xfffffff2u+IdleMs-1);ble_management_poll();assert(simulatedStopCalls==0);nowValue=uint32_t(0xfffffff2u+IdleMs);ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active());
 }else if(which=="advertising-wrap"){
  assert(ble_management_stop());simulatedStopCalls=0;nowValue=0xfffffff0u;assert(ble_management_begin());
  nowValue=uint32_t(0xfffffff0u+WindowMs-1);ble_management_poll();assert(simulatedStopCalls==0);nowValue=uint32_t(0xfffffff0u+WindowMs);ble_management_poll();assert(simulatedStopCalls==1&&!ble_management_active());
 }else return 2;
 simulatedStopOk=true;assert(ble_management_stop());std::puts("PASS production adapter timing case");
}
