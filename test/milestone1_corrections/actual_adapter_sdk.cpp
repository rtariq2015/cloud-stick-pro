// Host-only actual adapter + pinned SDK Security2 with reviewed byte-safe IV correction. SDK/radio lifecycle is
// stubbed, but SRP authentication and GCM encrypt/decrypt use real SDK C.
// No poll runs between the injected failure and next request: this models the
// allowed callback-before-main-poll ordering rather than a realtime scheduler.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sdk_adapter_stubs/common.h"
int simulatedStopCalls=0;
bool simulatedStopOk=true;
uint32_t millis(){return 100000;}
#include "../../src/ble_management.cpp"
static int from_hex(const char *hex,uint8_t *out,size_t cap) {
 size_t n=std::strlen(hex);if(n%2||n/2>cap)return -1;
 for(size_t i=0;i<n;i+=2){unsigned x;if(std::sscanf(hex+i,"%2x",&x)!=1)return -1;out[i/2]=uint8_t(x);}
 return int(n/2);
}
int main() {
 void* handle=nullptr;assert(ble_management_begin());
 assert(protocomm_security2.init(&handle)==ESP_OK);
 assert(newSession(handle,1)==ESP_OK);
 char line[4096];uint8_t input[1024];
 while(std::fgets(line,sizeof line,stdin)){
  line[std::strcspn(line,"\r\n")]=0;char* payload=std::strchr(line,' ');
  if(payload)*payload++=0;else payload=const_cast<char*>("");
  int len=from_hex(payload,input,sizeof input),ret=ESP_ERR_INVALID_ARG;
  uint8_t *out=nullptr;ssize_t outlen=0;
  if(!std::strcmp(line,"session")&&len>0)
   ret=secureHandshake(handle,&parameters,1,input,len,&out,&outlen,nullptr);
  else if(!std::strcmp(line,"info")&&len>=0){
   uint8_t* plain=nullptr;ssize_t plainlen=0;
   ret=decrypt(handle,1,input,len,&plain,&plainlen);
   if(ret==ESP_OK){
    uint8_t* response=nullptr;ssize_t responseLen=0;
    ret=info(1,plain,plainlen,&response,&responseLen,nullptr);
    if(ret==ESP_OK)ret=security().encrypt(handle,1,response,responseLen,&out,&outlen);
    std::free(response);
   }
   std::free(plain);
  }else if(!std::strcmp(line,"raw-info")&&len>=0)
    ret=info(1,input,len,&out,&outlen,nullptr);
  else if(!std::strcmp(line,"encrypt")&&len>=0)
    ret=security().encrypt(handle,1,input,len,&out,&outlen);
  else if(!std::strcmp(line,"close"))ret=closeSession(handle,1);
  else if(!std::strcmp(line,"new"))ret=newSession(handle,1);
  else if(!std::strcmp(line,"window")) {
    assert(ble_management_stop());
    assert(protocomm_security2.cleanup(handle)==ESP_OK);handle=nullptr;
    assert(ble_management_begin());
    assert(protocomm_security2.init(&handle)==ESP_OK);
    ret=newSession(handle,1);
  }else if(!std::strcmp(line,"poll")){ble_management_poll();ret=ESP_OK;}
  else if(!std::strcmp(line,"status"))ret=ESP_OK;
  else if(!std::strcmp(line,"quit"))break;
  std::printf("{\"error\":%d,\"data\":\"",ret);
  if(ret==ESP_OK&&out&&outlen>=0&&outlen<4096)
   for(ssize_t i=0;i<outlen;i++)std::printf("%02x",out[i]);
  std::printf("\",\"stopRequested\":%s,\"stopping\":%s,\"authenticated\":%s,\"handshakeCalls\":%u}\n",
   stopRequested.load()?"true":"false",stopping.load()?"true":"false",
   authenticated.load()?"true":"false",handshakeCalls.load());
  std::fflush(stdout);std::free(out);
 }
 assert(ble_management_stop());protocomm_security2.cleanup(handle);return 0;
}
