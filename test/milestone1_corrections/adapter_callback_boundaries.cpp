// Actual adapter callbacks with SDK-result/clock hooks. Crypto interoperability
// and bad-tag denial are separately tested against the actual pinned SDK C.
#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include "adapter_stubs/common.h"
int simulatedStopCalls=0;bool simulatedStopOk=true;
std::function<void()> onSdk,onClock;
uint32_t millis(){if(onClock){auto f=std::move(onClock);onClock=nullptr;f();}return 100000;}
static int session_ok(void*,uint32_t);
static int close_ok(void*,uint32_t){return ESP_OK;}
static int handshake_ok(void*,const void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*);
static int frame_ok(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*);
const protocomm_security_t protocomm_security2={2,1,nullptr,nullptr,session_ok,close_ok,handshake_ok,frame_ok,frame_ok};
#include "../../src/ble_management.cpp"
static void sdkHook(){if(onSdk){auto f=std::move(onSdk);onSdk=nullptr;f();}}
static int session_ok(void*,uint32_t){sdkHook();return ESP_OK;}
static int frame_ok(void*,uint32_t,const uint8_t*,ssize_t,uint8_t** output,ssize_t* n){
 *output=static_cast<uint8_t*>(malloc(3));*n=3;sdkHook();return ESP_OK;
}
static int handshake_ok(void* h,const void*,uint32_t id,const uint8_t* b,ssize_t n,uint8_t** o,ssize_t* l,void*){
 return frame_ok(h,id,b,n,o,l);
}
int main(int argc,char** argv){
 assert(argc==2);std::string scenario=argv[1];assert(ble_management_begin());
 if(scenario=="new-session"){
  onSdk=[] {reject(ESP_FAIL);};assert(newSession(nullptr,7)!=ESP_OK);
  assert(session.load()==NoSession && !connected.load());
 }else{
  assert(newSession(nullptr,7)==ESP_OK);
  uint8_t* output=nullptr;ssize_t n=0;uint8_t bytes[32]{};
  if(scenario=="handshake"){
   onSdk=[] {reject(ESP_FAIL);};
   assert(secureHandshake(nullptr,nullptr,7,bytes,sizeof bytes,&output,&n,nullptr)!=ESP_OK);
  }else if(scenario=="decrypt"){
   onSdk=[] {reject(ESP_FAIL);};
   assert(decrypt(nullptr,7,bytes,sizeof bytes,&output,&n)!=ESP_OK);
  }else if(scenario=="encrypt"){
   const char* request=R"({"v":1,"id":1,"op":"info"})";
   assert(info(7,reinterpret_cast<const uint8_t*>(request),std::strlen(request),&output,&n,nullptr)==ESP_OK);
   free(output);output=nullptr;n=0;
   onSdk=[] {reject(ESP_FAIL);};
   assert(encrypt(nullptr,7,bytes,sizeof bytes,&output,&n)!=ESP_OK);
  }else if(scenario=="info"){
   const char* request=R"({"v":1,"id":1,"op":"info"})";
   onClock=[] {reject(ESP_FAIL);};
   assert(info(7,reinterpret_cast<const uint8_t*>(request),std::strlen(request),&output,&n,nullptr)!=ESP_OK);
   assert(!authenticated.load());
  }else if(scenario=="replaced-session"){
   onSdk=[] {assert(closeSession(nullptr,7)==ESP_OK);assert(newSession(nullptr,8)==ESP_OK);};
   assert(decrypt(nullptr,7,bytes,sizeof bytes,&output,&n)!=ESP_OK);
   assert(session.load()==8 && !stopRequested.load());
  }else return 2;
  assert(!output && n==0);
 }
 assert(ble_management_stop());
 std::puts(("PASS terminal/stale response discarded: "+scenario).c_str());
}
