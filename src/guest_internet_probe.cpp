#include "guest_internet_probe.h"
#if CSP_ENABLE_GUEST_DIAGNOSTIC
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <esp_err.h>
#include <cstdio>
#include <lwip/netdb.h>
#include <atomic>
#include <cstring>
#include <ctime>
namespace pro::guest_diag {
namespace {
constexpr size_t StackBytes=16384;
constexpr uint32_t IoTimeoutMs=5000,ClockWaitMs=20000;
portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t owner=nullptr,task=nullptr;
BaseType_t core=-1;
StackType_t* stack=nullptr;StaticTask_t* control=nullptr;
std::atomic<bool> cancelled{false},handoff{false},drained{false};
InternetResult result;
bool owns(){return !owner||(owner==xTaskGetCurrentTaskHandle()&&core==xPortGetCoreID()&&xTaskGetCoreID(owner)==core);}
bool stopped(){return cancelled.load(std::memory_order_acquire)||WiFi.status()!=WL_CONNECTED;}
bool resolve(const char* hostname,IPAddress& ip){
  // Pinned lwIP resolver has finite retries. It runs only in this owned task;
  // cancellation waits for its ordinary return, never frees a live callback.
  addrinfo hints{},*answer=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
  const int error=lwip_getaddrinfo(hostname,nullptr,&hints,&answer);
  const bool ok=error==0&&answer&&answer->ai_addr&&answer->ai_addrlen>=sizeof(sockaddr_in);
  if(ok)ip=IPAddress(reinterpret_cast<sockaddr_in*>(answer->ai_addr)->sin_addr.s_addr);
  if(answer)lwip_freeaddrinfo(answer);
  return ok;
}
// Only a status line is retained. Read exactly through CRLFCRLF, never bodies,
// Location or cookie values. The byte budget and whole-header deadline also
// bound a portal that keeps sending small pieces without completing headers.
template<class Client> int headers(Client& client,int& error){
  char first[96]{};size_t first_size=0;bool first_done=false;
  uint32_t tail=0;const uint32_t started=millis();
  for(size_t bytes=0;bytes<4096;){
    if(stopped()){error=ESP_FAIL;return 0;}
    if(uint32_t(millis()-started)>=IoTimeoutMs){error=ESP_ERR_TIMEOUT;return 0;}
    if(!client.available()){
      if(!client.connected()){error=ESP_FAIL;return 0;}
      vTaskDelay(pdMS_TO_TICKS(10));continue;
    }
    const int value=client.read();if(value<0){error=ESP_FAIL;return 0;}
    ++bytes;const char c=char(value);
    if(!first_done){
      if(c=='\n')first_done=true;
      else if(first_size+1<sizeof first)first[first_size++]=c;
      else {error=ESP_ERR_INVALID_SIZE;return 0;}
    }
    tail=(tail<<8)|uint8_t(value);
    if(tail==0x0d0a0d0a){
      if(first_size<13||first[first_size-1]!='\r'||
         (std::strncmp(first,"HTTP/1.1 ",9)&&std::strncmp(first,"HTTP/1.0 ",9))||
         first[9]<'1'||first[9]>'5'||first[10]<'0'||first[10]>'9'||first[11]<'0'||first[11]>'9'||
         (first[12]!=' '&&first[12]!='\r')){error=ESP_FAIL;return 0;}
      error=ESP_OK;return (first[9]-'0')*100+(first[10]-'0')*10+first[11]-'0';
    }
  }
  error=ESP_ERR_INVALID_SIZE;return 0;
}
template<class Client> int exchange(Client& client,bool secure,int& error){
  const char* get=secure?"GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n":
    "GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\nConnection: close\r\n\r\n";
  const size_t n=std::strlen(get);
  if(stopped()||client.write(reinterpret_cast<const uint8_t*>(get),n)!=n){error=ESP_FAIL;return 0;}
  return headers(client,error);
}
void request(bool secure,const IPAddress& ip,InternetResult& out){
  if(secure){
    NetworkClientSecure client;
    // Frozen Mozilla bundle plus the actual DNS hostname: normal chain, name
    // and validity-date verification remains mandatory. No insecure fallback.
    client.useBuiltinCACertBundle();client.setHandshakeTimeout(5);client.setTimeout(IoTimeoutMs);
    if(!client.connect("www.google.com",443,IoTimeoutMs))out.https_error=ESP_FAIL;
    else out.https_status=exchange(client,true,out.https_error);
    char unused[1]{};out.tls_error=client.lastError(unused,sizeof unused);
    out.https_verified=out.https_error==ESP_OK&&out.https_status>0&&out.tls_error==0;
    client.stop();
  }else{
    NetworkClient client;client.setTimeout(IoTimeoutMs);
    if(!client.connect(ip,80,IoTimeoutMs))out.http_error=ESP_FAIL;
    else out.http_status=exchange(client,false,out.http_error);
    client.stop();
  }
}
InternetResult run(){
  InternetResult out;
  if(stopped())return out;
  IPAddress http_ip,https_ip;
  out.dns_http=resolve("connectivitycheck.gstatic.com",http_ip);
  if(out.dns_http&&!stopped())request(false,http_ip,out);
  if(stopped())return out;
  out.dns_https=resolve("www.google.com",https_ip);
  const auto start=millis();
  // Runtime setup starts the existing configured SNTP service. This diagnostic
  // waits for it but never changes its server, timezone or saved configuration.
  while(!stopped()&&time(nullptr)<1767225600&&uint32_t(millis()-start)<ClockWaitMs)vTaskDelay(pdMS_TO_TICKS(100));
  out.clock_valid=time(nullptr)>=1767225600;
  if(out.dns_https&&out.clock_valid&&!stopped())request(true,https_ip,out);
  return out;
}
void entry(void*){
  while(!handoff.load(std::memory_order_acquire))vTaskDelay(1);
  const auto completed=run();
  portENTER_CRITICAL(&mutex);result=completed;portEXIT_CRITICAL(&mutex);
  drained.store(true,std::memory_order_release);
  for(;;)vTaskSuspend(nullptr);
}
}
bool internetStart(){
  if(!owns()||task||stack||control||heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<96*1024||
     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<64*1024)return false;
  const auto caller=xTaskGetCurrentTaskHandle();const auto on=xPortGetCoreID();
  if(!caller||xTaskGetCoreID(caller)!=on)return false;
  owner=caller;core=on;cancelled=false;handoff=false;drained=false;
  stack=static_cast<StackType_t*>(heap_caps_malloc(StackBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  control=static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(stack&&control)task=xTaskCreateStaticPinnedToCore(entry,"GuestInternet",StackBytes,nullptr,1,stack,control,core);
  if(!task){heap_caps_free(stack);heap_caps_free(control);stack=nullptr;control=nullptr;return false;}
  handoff.store(true,std::memory_order_release);return true;
}
bool internetBusy(){return task||stack||control;}
void internetCancel(){if(owns())cancelled.store(true,std::memory_order_release);}
bool internetReap(InternetResult& out){
  if(!owns()||!task||!drained.load(std::memory_order_acquire)||eTaskGetState(task)!=eSuspended)return false;
  portENTER_CRITICAL(&mutex);out=result;portEXIT_CRITICAL(&mutex);
  vTaskDelete(task);task=nullptr;heap_caps_free(stack);heap_caps_free(control);stack=nullptr;control=nullptr;return true;
}
}
#endif
