#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1;
using TaskHandle_t=void*;
using wifi_mode_t=int;
constexpr int WIFI_MODE_NULL=0,WIFI_MODE_STA=1,WIFI_MODE_AP=2,WIFI_MODE_APSTA=3;
constexpr int WIFI_STORAGE_RAM=0,WIFI_IF_AP=1,WIFI_AUTH_WPA2_PSK=3,WIFI_CIPHER_TYPE_CCMP=4,WL_CONNECTED=3,IPv4=4;
constexpr int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
using esp_event_base_t=const char*;
// Real ESP event bases have one shared identity across translation units.
// A constexpr pointer to a literal can differ across native ASan TUs.
inline constexpr char WifiEventIdentity[]="wifi";
inline constexpr esp_event_base_t WIFI_EVENT=WifiEventIdentity;
constexpr int ESP_EVENT_ANY_ID=-1,WIFI_EVENT_AP_START=10,WIFI_EVENT_AP_STOP=11;
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t const name=#name
struct IPAddress {
  uint32_t value=0;
  IPAddress()=default;
  IPAddress(unsigned a,unsigned b,unsigned c,unsigned d):value(a<<24|b<<16|c<<8|d){}
  unsigned operator[](unsigned i)const{return (value>>(24-i*8))&255;}
  int type()const{return IPv4;}
};
struct esp_netif_t {int id;};
struct esp_netif_ip_info_t {struct {uint32_t addr;}ip{},netmask{},gw{};};
struct wifi_ap_record_t {uint8_t bssid[6]{1,2,3,4,5,6};};
struct wifi_sta_list_t {unsigned num=0;};
struct wifi_config_t {struct {uint8_t ssid[32]{},password[64]{};unsigned ssid_len=0,max_connection=0;int authmode=0;}ap;};
using EventCallback=void(*)(void*,esp_event_base_t,int32_t,void*);
struct Handler {esp_event_base_t base;int32_t id;EventCallback callback;void* context;};
using esp_event_handler_instance_t=Handler*;
namespace route_fake {
inline uint32_t now=0;
inline void (*modeReadHook)()=nullptr;
inline TaskHandle_t current=reinterpret_cast<void*>(1);
inline esp_netif_t station{1},ap{2};
inline wifi_mode_t mode=WIFI_MODE_STA;
inline wifi_config_t config;
inline uint32_t ip=0x0a000002,mask=0xffffff00,gateway=0x0a000001,dns=0x08080808,ap_ip=0;
inline uint8_t bssid[6]{1,2,3,4,5,6};
inline bool connected=true,apUp=false,napt=false,allocationFail=false,createFail=false,configFail=false,configMismatch=false;
inline bool naptEnableFail=false,naptDisableFail=false,apStopFail=false,suppressStop=false,dropBarriers=false;
inline bool unregisterFail=false,clientsFail=false,deauthFail=false,storageFail=false,keepClients=false;
inline unsigned clients=0,registrations=0,failRegisterAt=0,posts=0,failPostAt=0;
inline unsigned creates=0,ends=0,enableCalls=0,disableCalls=0,deauthCalls=0,allocations=0,frees=0;
inline int nextFd=100,failSocketAt=-1,socketCalls=0,failBindFd=-1,failCloseFd=-1;
inline std::map<int,unsigned> closeCalls;
inline std::set<void*> buffers;
inline std::vector<Handler*> handlers;
struct Queued {esp_event_base_t base;int32_t id;uint32_t token=0;};
inline std::deque<Queued> events;
struct Datagram {uint32_t ip;uint16_t port;std::vector<uint8_t> bytes;};
struct Socket {uint32_t ip=0;uint16_t port=0;std::deque<Datagram> incoming;std::vector<Datagram> sent;};
inline std::map<int,Socket> sockets;
inline void emit(int32_t id){events.push_back({WIFI_EVENT,id,0});}
inline void pump(){
  size_t cap=100;
  while(!events.empty()&&cap--){
    auto event=events.front();events.pop_front();
    if(dropBarriers&&event.base!=WIFI_EVENT)continue;
    for(auto* h:handlers)if(h->base==event.base&&(h->id==ESP_EVENT_ANY_ID||h->id==event.id))
      h->callback(h->context,event.base,event.id,&event.token);
  }
}
}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return route_fake::current;}
inline uint32_t millis(){return route_fake::now;}
inline uint32_t esp_random(){return 0x1234;}
inline void* heap_caps_malloc(size_t n,unsigned){
  if(route_fake::allocationFail)return nullptr;
  void* p=std::malloc(n);if(p){route_fake::buffers.insert(p);++route_fake::allocations;}return p;
}
inline void heap_caps_free(void* p){assert(route_fake::buffers.erase(p)==1);++route_fake::frees;std::free(p);}
inline int esp_event_handler_instance_register(esp_event_base_t base,int32_t id,EventCallback callback,void* ctx,Handler** out){
  if(++route_fake::registrations==route_fake::failRegisterAt)return ESP_FAIL;
  *out=new Handler{base,id,callback,ctx};route_fake::handlers.push_back(*out);return ESP_OK;
}
inline int esp_event_handler_instance_unregister(esp_event_base_t,int32_t,Handler* h){
  if(route_fake::unregisterFail)return ESP_FAIL;
  auto& list=route_fake::handlers;auto at=std::find(list.begin(),list.end(),h);assert(at!=list.end());list.erase(at);delete h;return ESP_OK;
}
inline int esp_event_post(esp_event_base_t base,int32_t id,const void* data,size_t size,unsigned wait){
  assert(size==4&&wait==0);
  if(++route_fake::posts==route_fake::failPostAt)return ESP_FAIL;
  uint32_t token;std::memcpy(&token,data,4);route_fake::events.push_back({base,id,token});return ESP_OK;
}
inline int esp_wifi_get_mode(wifi_mode_t* out){*out=route_fake::mode;if(auto hook=route_fake::modeReadHook)hook();return ESP_OK;}
inline int esp_wifi_set_storage(int storage){assert(storage==WIFI_STORAGE_RAM);return route_fake::storageFail?ESP_FAIL:ESP_OK;}
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t* out){std::memcpy(out->bssid,route_fake::bssid,6);return route_fake::connected?ESP_OK:ESP_FAIL;}
inline int esp_wifi_get_config(int which,wifi_config_t* out){assert(which==WIFI_IF_AP);*out=route_fake::config;if(route_fake::configMismatch)out->ap.authmode=0;return ESP_OK;}
inline int esp_wifi_ap_get_sta_list(wifi_sta_list_t* out){out->num=route_fake::clients;return route_fake::clientsFail?ESP_FAIL:ESP_OK;}
inline int esp_wifi_deauth_sta(unsigned aid){
  assert(aid==0);++route_fake::deauthCalls;
  if(!route_fake::keepClients)route_fake::clients=0;
  return route_fake::deauthFail?ESP_FAIL:ESP_OK;
}
inline int esp_netif_get_ip_info(esp_netif_t* netif,esp_netif_ip_info_t* out){
  assert(netif==&route_fake::station||netif==&route_fake::ap);
  out->ip.addr=htonl(netif->id==1?route_fake::ip:route_fake::ap_ip);
  out->netmask.addr=htonl(netif->id==1?route_fake::mask:0xffffff00);
  out->gw.addr=htonl(netif->id==1?route_fake::gateway:route_fake::ap_ip);return ESP_OK;
}
inline bool esp_netif_is_netif_up(esp_netif_t* netif){
  assert(netif==&route_fake::station||netif==&route_fake::ap);
  return netif==&route_fake::ap?route_fake::apUp:route_fake::connected;
}
inline int esp_netif_napt_enable(esp_netif_t* n){assert(n==&route_fake::ap);++route_fake::enableCalls;route_fake::napt=true;return route_fake::naptEnableFail?ESP_FAIL:ESP_OK;}
inline int esp_netif_napt_disable(esp_netif_t* n){
  assert(n==&route_fake::ap);++route_fake::disableCalls;
  if(route_fake::naptDisableFail)return ESP_FAIL;route_fake::napt=false;return ESP_OK;
}
struct FakeSTA {esp_netif_t* netif(){return &route_fake::station;}};
struct FakeAP {
  bool create(const char* name,const char* password,int,int,unsigned max,bool,int auth,int cipher){
    assert(max==1&&auth==WIFI_AUTH_WPA2_PSK&&cipher==WIFI_CIPHER_TYPE_CCMP);
    ++route_fake::creates;route_fake::mode=WIFI_MODE_APSTA;route_fake::apUp=true;route_fake::emit(WIFI_EVENT_AP_START);
    route_fake::config={};auto& ap=route_fake::config.ap;ap.ssid_len=std::strlen(name);
    std::memcpy(ap.ssid,name,ap.ssid_len);std::memcpy(ap.password,password,std::strlen(password)+1);
    ap.authmode=auth;ap.max_connection=max;return !route_fake::createFail;
  }
  bool config(IPAddress ip,IPAddress gateway,IPAddress mask,IPAddress lease,IPAddress dns){
    assert(ip.value==gateway.value&&mask.value==0xffffff00&&lease.value==ip.value+1&&dns.value==ip.value);
    route_fake::ap_ip=ip.value;return !route_fake::configFail;
  }
  esp_netif_t* netif(){return &route_fake::ap;}
  bool end(){
    ++route_fake::ends;if(route_fake::apStopFail)return false;
    const bool was=(route_fake::mode&WIFI_MODE_AP);route_fake::mode=WIFI_MODE_STA;route_fake::apUp=false;
    if(was&&!route_fake::suppressStop)route_fake::emit(WIFI_EVENT_AP_STOP);return true;
  }
};
struct FakeWiFi {
  FakeSTA STA;FakeAP AP;
  int status(){return route_fake::connected?WL_CONNECTED:0;}
  IPAddress dnsIP(unsigned i){const auto dns=i?0:route_fake::dns;return IPAddress(dns>>24,(dns>>16)&255,(dns>>8)&255,dns&255);}
};
inline FakeWiFi WiFi;
inline int owned_fake_socket(int family,int type,int protocol){
  assert(family==AF_INET&&type==SOCK_DGRAM&&protocol==IPPROTO_UDP);
  if(route_fake::socketCalls++==route_fake::failSocketAt){errno=ENOMEM;return -1;}
  int fd=route_fake::nextFd++;route_fake::sockets.emplace(fd,route_fake::Socket{});return fd;
}
inline int owned_fake_ioctl(int fd,unsigned long request,void* value){assert(route_fake::sockets.count(fd)&&request==FIONBIO&&*static_cast<int*>(value)==1);return 0;}
inline int owned_fake_bind(int fd,const sockaddr* address,socklen_t size){
  assert(size==sizeof(sockaddr_in));if(fd==route_fake::failBindFd){errno=EADDRNOTAVAIL;return -1;}
  const auto* to=reinterpret_cast<const sockaddr_in*>(address);route_fake::sockets.at(fd).ip=ntohl(to->sin_addr.s_addr);
  route_fake::sockets.at(fd).port=ntohs(to->sin_port);return 0;
}
inline int owned_fake_close(int fd){
  ++route_fake::closeCalls[fd];if(fd==route_fake::failCloseFd){errno=EIO;return -1;}
  assert(route_fake::sockets.erase(fd)==1);return 0;
}
inline int owned_fake_recvfrom(int fd,void* buffer,size_t size,int flags,sockaddr* address,socklen_t* length){
  assert(flags==MSG_DONTWAIT);auto& socket=route_fake::sockets.at(fd);
  if(socket.incoming.empty()){errno=EAGAIN;return -1;}
  auto frame=socket.incoming.front();socket.incoming.pop_front();
  const auto count=std::min(size,frame.bytes.size());if(count)std::memcpy(buffer,frame.bytes.data(),count);
  auto* from=reinterpret_cast<sockaddr_in*>(address);*from={};from->sin_family=AF_INET;from->sin_port=htons(frame.port);
  from->sin_addr.s_addr=htonl(frame.ip);*length=sizeof *from;return int(count);
}
inline int owned_fake_sendto(int fd,const void* buffer,size_t size,int flags,const sockaddr* address,socklen_t length){
  assert(flags==MSG_DONTWAIT&&length==sizeof(sockaddr_in));const auto* to=reinterpret_cast<const sockaddr_in*>(address);
  const auto* bytes=static_cast<const uint8_t*>(buffer);
  route_fake::sockets.at(fd).sent.push_back({ntohl(to->sin_addr.s_addr),ntohs(to->sin_port),{bytes,bytes+size}});return int(size);
}
