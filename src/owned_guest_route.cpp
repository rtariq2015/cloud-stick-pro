#include "owned_guest_route.h"
#if CSP_ENABLE_OWNED_GUEST_ROUTE
#include "pro_network.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <new>

namespace pro::owned_guest {
namespace {
constexpr size_t PacketLimit=1232,QuestionLimit=259,PendingLimit=4;
constexpr uint32_t DnsTimeout=3000,TransitionTimeout=5000,StopTimeout=2000,MaxWindow=300000;
constexpr int32_t BarrierId=1;
ESP_EVENT_DEFINE_BASE(CSP_OWNED_GUEST_EVENTS);
struct EventState {
  std::atomic<uint32_t> expected{0},seen{0};
  // Low bit is the latest AP event (1=START). Upper bits count STOP events.
  // One atomic publication avoids losing a stop across startup admission.
  std::atomic<uint32_t> ap_epoch{0};
  std::atomic<bool> ap_epoch_exhausted{false};
  std::atomic<bool> enabled{false},ap_started{false},ap_stopped{false};
} events; // Small static context: queued callbacks never point at freed DNS RAM.
struct Pending {
  bool used=false;uint32_t client=0,started=0;
  uint16_t port=0,original=0,nonce=0,length=0;uint8_t question[QuestionLimit]{};
};
struct Buffers {uint8_t packet[PacketLimit+1]{};Pending pending[PendingLimit]{};};
enum class Step:uint8_t {PreBarrier,Configure,EmptyClients,ReadyBarrier,Stopping,StopBarrier};
Status state;
TaskHandle_t owner=nullptr;
Buffers* buffers=nullptr;
esp_netif_t* ap_netif=nullptr;
esp_event_handler_instance_t wifi_handler=nullptr,barrier_handler=nullptr;
int ap_socket=-1,up_socket=-1;
Uplink uplink;
uint8_t station_bssid[6]{};
char ap_name[33]{},ap_password[64]{};
uint32_t started=0,step_at=0,window=0,barrier_counter=0;
Step step=Step::PreBarrier;
uint32_t admitted_stop_epoch=0;
#if CSP_PRODUCT_LAB6_PROFILE
Diagnostic diagnostic_state;
#endif
bool ap_touched=false,napt_attempted=false,cleanup_fault=false,stop_issued=false,need_ap_stop=false;

void erase(void* p,size_t n){auto* b=static_cast<volatile uint8_t*>(p);while(n--)*b++=0;}
bool elapsed(uint32_t now,uint32_t before,uint32_t interval){return uint32_t(now-before)>=interval;}
bool owns(){return !owner||owner==xTaskGetCurrentTaskHandle();}
bool usable(uint32_t ip){return (ip>>24)&&((ip>>24)!=127)&&(ip>>24)<224&&ip!=0xffffffffU;}
uint32_t number(const IPAddress& ip){return uint32_t(ip[0])<<24|uint32_t(ip[1])<<16|uint32_t(ip[2])<<8|ip[3];}
IPAddress address(uint32_t ip){return IPAddress(ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255);}
bool currentUplink(bool capture=false){
  esp_netif_ip_info_t info{};wifi_ap_record_t station{};
  if(WiFi.status()!=WL_CONNECTED||!WiFi.STA.netif()||
     esp_netif_get_ip_info(WiFi.STA.netif(),&info)!=ESP_OK||esp_wifi_sta_get_ap_info(&station)!=ESP_OK)return false;
  const auto first=WiFi.dnsIP(0),second=WiFi.dnsIP(1);
  if(ntohl(info.ip.addr)!=uplink.ip||ntohl(info.netmask.addr)!=uplink.mask||ntohl(info.gw.addr)!=uplink.gateway||
     !((first.type()==IPv4&&number(first)==uplink.dns)||(second.type()==IPv4&&number(second)==uplink.dns)))return false;
  if(capture)std::memcpy(station_bssid,station.bssid,sizeof station_bssid);
  return !std::memcmp(station_bssid,station.bssid,sizeof station_bssid);
}
bool credentials(std::string_view name,std::string_view password){
  if(name.empty()||name.size()>32||password.size()<8||password.size()>63)return false;
  for(unsigned char c:name)if(c<32||c==127)return false;
  for(unsigned char c:password)if(c<32||c>126)return false;
  return true;
}
void event(void*,esp_event_base_t base,int32_t id,void* data){
  if(!events.enabled.load(std::memory_order_acquire))return;
  if(base==WIFI_EVENT){
    if(id==WIFI_EVENT_AP_START){
      events.ap_started.store(true,std::memory_order_release);
      events.ap_epoch.fetch_or(1,std::memory_order_acq_rel);
    }
    if(id==WIFI_EVENT_AP_STOP){
      events.ap_stopped.store(true,std::memory_order_release);
      auto prior=events.ap_epoch.load(std::memory_order_acquire);
      for(;;){
        if((prior&~uint32_t(1))==UINT32_MAX-1){
          events.ap_epoch_exhausted.store(true,std::memory_order_release);
          events.ap_epoch.fetch_and(~uint32_t(1),std::memory_order_acq_rel);break;
        }
        if(events.ap_epoch.compare_exchange_weak(prior,(prior&~uint32_t(1))+2,
            std::memory_order_acq_rel,std::memory_order_acquire))break;
      }
    }
  }else if(base==CSP_OWNED_GUEST_EVENTS&&id==BarrierId&&data){
    uint32_t token;std::memcpy(&token,data,sizeof token);
    if(token==events.expected.load(std::memory_order_acquire))events.seen.store(token,std::memory_order_release);
  }
}
bool postBarrier(){
  if(barrier_counter==UINT32_MAX)return false;
  const auto token=++barrier_counter;events.expected.store(token,std::memory_order_release);
  events.seen.store(0,std::memory_order_release);
  const auto result=esp_event_post(CSP_OWNED_GUEST_EVENTS,BarrierId,&token,sizeof token,0);
  if(result!=ESP_OK){state.native_error=result;return false;}return true;
}
bool barrierSeen(){return events.seen.load(std::memory_order_acquire)==events.expected.load(std::memory_order_acquire);}
void fault(Error error,int native=0){cleanup_fault=true;state.error=error;state.native_error=native;}
void beginStop(Error error=Error::None,int native=0){
  if(state.phase==Phase::Stopping||state.phase==Phase::Blocked)return;
#if CSP_PRODUCT_LAB6_PROFILE
  diagnostic_state.failure_phase=unsigned(state.phase);diagnostic_state.failure_step=unsigned(step);
#endif
  if(error!=Error::None){state.error=error;state.native_error=native;}
  state.phase=Phase::Stopping;step=Step::Stopping;step_at=millis();stop_issued=false;
  erase(ap_name,sizeof ap_name);erase(ap_password,sizeof ap_password);
}
bool mode(wifi_mode_t& value){const auto err=esp_wifi_get_mode(&value);if(err!=ESP_OK){state.native_error=err;return false;}return true;}
bool apLive(bool admitting=false){
  wifi_mode_t current=WIFI_MODE_NULL;
  const auto observed=events.ap_epoch.load(std::memory_order_acquire);
  // AP.create first starts its prior configuration, then configures the private
  // AP. A completed START/STOP/START during this controlled startup is not an
  // Active-route failure. Capture the stop counter; never clear a stop latch
  // after checking it, since a concurrent stop could otherwise disappear.
  const bool up=mode(current)&&current==WIFI_MODE_APSTA&&ap_netif&&esp_netif_is_netif_up(ap_netif);
  const bool started_event=events.ap_started.load(std::memory_order_acquire);
#if CSP_PRODUCT_LAB6_PROFILE
  const bool stopped_event=events.ap_stopped.load(std::memory_order_acquire);
  diagnostic_state.ap_proof=(up?1:0)|(started_event?2:0)|(stopped_event?4:0);
#endif
  const bool ready=up&&started_event&&(observed&1)&&!events.ap_epoch_exhausted.load(std::memory_order_acquire);
  if(admitting){
    if(ready)admitted_stop_epoch=observed>>1;
    return ready;
  }
  // Every STOP after the captured admission remains terminal, including a
  // STOP immediately followed by START or one racing the admission read.
  return ready&&(observed>>1)==admitted_stop_epoch;
}
bool closeSocket(int& fd){
  if(fd<0)return true;
  if(::close(fd)!=0){fault(Error::CloseSocket,errno);return false;}
  fd=-1;return true;
}
bool udp(int& fd,uint32_t local,uint16_t port){
  fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(fd<0)return false;
  sockaddr_in bind_to{};bind_to.sin_family=AF_INET;bind_to.sin_port=htons(port);bind_to.sin_addr.s_addr=htonl(local);
  int enabled=1;
  // Keep ownership on setup failure; only the checked stop path closes a
  // descriptor, exactly once. Never retry an ambiguous failed close.
  return ::ioctl(fd,FIONBIO,&enabled)==0&&::bind(fd,reinterpret_cast<sockaddr*>(&bind_to),sizeof bind_to)==0;
}
bool configure(){
  // AP.begin may briefly activate its previous config; no NAPT/DNS listener is
  // admitted until private WPA2 readback and all startup associations are gone.
  ap_touched=true;
  if(!WiFi.AP.create(ap_name,ap_password,1,0,1,false,WIFI_AUTH_WPA2_PSK,WIFI_CIPHER_TYPE_CCMP))return false;
  ap_netif=WiFi.AP.netif();if(!ap_netif)return false;
  wifi_config_t config{};
  const auto read=esp_wifi_get_config(WIFI_IF_AP,&config);
  const bool private_config=read==ESP_OK&&config.ap.authmode==WIFI_AUTH_WPA2_PSK&&config.ap.max_connection==1&&
    config.ap.ssid_len==std::strlen(ap_name)&&!std::memcmp(config.ap.ssid,ap_name,config.ap.ssid_len)&&
    !std::memcmp(config.ap.password,ap_password,std::strlen(ap_password)+1);
  erase(&config,sizeof config);erase(ap_name,sizeof ap_name);erase(ap_password,sizeof ap_password);
  if(!private_config)return false;
  const auto ap=address(state.ap_ip),mask=address(pro::SetupMask),lease=address(state.ap_ip+1);
  if(!WiFi.AP.config(ap,ap,mask,lease,ap))return false;
  esp_netif_ip_info_t info{};
  if(esp_netif_get_ip_info(ap_netif,&info)!=ESP_OK||ntohl(info.ip.addr)!=state.ap_ip||
     ntohl(info.gw.addr)!=state.ap_ip||ntohl(info.netmask.addr)!=pro::SetupMask)return false;
  return esp_wifi_deauth_sta(0)==ESP_OK;
}
uint16_t read16(const uint8_t* p){return uint16_t(p[0])<<8|p[1];}
void write16(uint8_t* p,uint16_t n){p[0]=n>>8;p[1]=n&255;}
bool question(const uint8_t* p,size_t n,size_t& count){
  if(n<17||n>PacketLimit||read16(p+4)!=1||(p[2]&0x78))return false;
  size_t at=12;
  while(at<n){
    const uint8_t label=p[at++];if(label>63)return false;
    if(!label){if(at-12>255||at+4>n||!read16(p+at)||read16(p+at+2)!=1)return false;count=at+4-12;return count<=QuestionLimit;}
    if(at+label>n||at+label-12>=255)return false;at+=label;
  }
  return false;
}
bool client(uint32_t ip){return (ip&pro::SetupMask)==(state.ap_ip&pro::SetupMask)&&(ip&255)>=2&&(ip&255)<=254;}
int receive(int fd,sockaddr_in& from){
  socklen_t size=sizeof from;
  const int n=::recvfrom(fd,buffers->packet,sizeof buffers->packet,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&from),&size);
  if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)beginStop(Error::Socket,errno);return 0;}
  return size==sizeof from&&from.sin_family==AF_INET&&n<=int(PacketLimit)?n:-1;
}
bool send(int fd,uint32_t ip,uint16_t port,size_t size){
  sockaddr_in to{};to.sin_family=AF_INET;to.sin_addr.s_addr=htonl(ip);to.sin_port=htons(port);
  return ::sendto(fd,buffers->packet,size,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&to),sizeof to)==int(size);
}
void dns(uint32_t now){
  for(auto& pending:buffers->pending)if(pending.used&&elapsed(now,pending.started,DnsTimeout))pending.used=false;
  for(size_t i=0;i<PendingLimit&&state.phase==Phase::Active;++i){
    sockaddr_in from{};const auto n=receive(up_socket,from);if(!n)break;
    if(n<0||ntohl(from.sin_addr.s_addr)!=uplink.dns||ntohs(from.sin_port)!=53)continue;
    size_t length=0;const auto* p=buffers->packet;
    if(!(p[2]&0x80)||!question(p,size_t(n),length))continue;
    for(auto& item:buffers->pending)if(item.used&&read16(p)==item.nonce&&length==item.length&&!std::memcmp(p+12,item.question,length)){
      write16(buffers->packet,item.original);if(send(ap_socket,item.client,item.port,size_t(n)))++state.replies;item.used=false;break;
    }
  }
  for(size_t i=0;i<PendingLimit&&state.phase==Phase::Active;++i){
    sockaddr_in from{};const auto n=receive(ap_socket,from);if(!n)break;
    if(n<0||!client(ntohl(from.sin_addr.s_addr))||!ntohs(from.sin_port))continue;
    size_t length=0;const auto* p=buffers->packet;
    if(!question(p,size_t(n),length)||(p[2]&0x80)||(p[3]&15)||read16(p+6)||read16(p+8))continue;
    Pending* slot=nullptr;bool duplicate=false;
    for(auto& item:buffers->pending){
      if(!item.used){if(!slot)slot=&item;continue;}
      if(item.client==ntohl(from.sin_addr.s_addr)&&item.port==ntohs(from.sin_port)&&item.original==read16(p)&&
         item.length==length&&!std::memcmp(item.question,p+12,length))duplicate=true;
    }
    if(!slot||duplicate)continue;
    uint16_t nonce=uint16_t(esp_random());
    for(size_t attempt=0;attempt<=PendingLimit;++attempt){
      bool used=false;for(const auto& item:buffers->pending)if(item.used&&item.nonce==nonce)used=true;
      if(!used)break;++nonce;
    }
    slot->client=ntohl(from.sin_addr.s_addr);slot->port=ntohs(from.sin_port);slot->original=read16(p);
    slot->nonce=nonce;slot->length=length;slot->started=now;std::memcpy(slot->question,p+12,length);
    write16(buffers->packet,nonce);slot->used=send(up_socket,uplink.dns,53,size_t(n));if(slot->used)++state.queries;
  }
}
void stopping(uint32_t now){
  if(!stop_issued){
    stop_issued=true;
    if(napt_attempted){
      const auto err=ap_netif?esp_netif_napt_disable(ap_netif):ESP_FAIL;
      if(err!=ESP_OK)fault(Error::DisableNapt,err);else napt_attempted=false;
    }
    closeSocket(ap_socket);closeSocket(up_socket);
    wifi_mode_t current=WIFI_MODE_NULL;
    if(!mode(current)){fault(Error::ApProof,state.native_error);need_ap_stop=true;}
    else need_ap_stop=(current&WIFI_MODE_AP)||events.ap_started.load();
    if(ap_touched&&!WiFi.AP.end())fault(Error::ApStop);
  }
  if(step==Step::Stopping){
    wifi_mode_t current=WIFI_MODE_NULL;
    const bool off=mode(current)&&!(current&WIFI_MODE_AP);
    if(off&&(!need_ap_stop||events.ap_stopped.load(std::memory_order_acquire))){
      if(!postBarrier()){fault(Error::EventBarrier,state.native_error);state.phase=Phase::Blocked;return;}
      step=Step::StopBarrier;
    }
  }
  if(step==Step::StopBarrier&&barrierSeen()){
    if(wifi_handler){const auto err=esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler);
      if(err!=ESP_OK)fault(Error::EventUnregister,err);else wifi_handler=nullptr;}
    if(barrier_handler){const auto err=esp_event_handler_instance_unregister(CSP_OWNED_GUEST_EVENTS,BarrierId,barrier_handler);
      if(err!=ESP_OK)fault(Error::EventUnregister,err);else barrier_handler=nullptr;}
    if(cleanup_fault){state.phase=Phase::Blocked;return;}
    events.enabled.store(false,std::memory_order_release);
    if(buffers){erase(buffers,sizeof *buffers);buffers->~Buffers();heap_caps_free(buffers);buffers=nullptr;}
    ap_netif=nullptr;state.station_preserved=currentUplink();state.resources_retained=false;state.phase=Phase::Stopped;
    if(!state.station_preserved&&state.error==Error::None)state.error=Error::UplinkChanged;
    return;
  }
  if(elapsed(now,step_at,StopTimeout)){fault(Error::Deadline);state.phase=Phase::Blocked;}
}
} // namespace

bool start(const Request& request){
  if(!owns()||state.phase!=Phase::Stopped)return false;
#if CSP_PRODUCT_LAB6_PROFILE
  diagnostic_state={};
#endif
  if(!credentials(request.ap_name,request.ap_password)||request.window_ms<1000||request.window_ms>MaxWindow||
     !usable(request.uplink.ip)||!usable(request.uplink.gateway)||!usable(request.uplink.dns)||
     !pro::validMask(request.uplink.mask)||(request.uplink.ip&request.uplink.mask)!=(request.uplink.gateway&request.uplink.mask)){
    state.error=Error::InvalidRequest;return false;
  }
  wifi_mode_t current=WIFI_MODE_NULL;
  if(!mode(current)||current!=WIFI_MODE_STA){state.error=Error::ApProof;return false;}
  uplink=request.uplink;if(!currentUplink(true)){state.error=Error::UplinkChanged;return false;}
  const auto ap=pro::chooseSetupAddress(uplink.ip,uplink.mask,uplink.dns);
  if(!ap){state.error=Error::NoSubnet;return false;}
  if(state.generation==UINT32_MAX){state.error=Error::Deadline;return false;}
  owner=xTaskGetCurrentTaskHandle();if(!owner){state.error=Error::Owner;return false;}
  const auto generation=state.generation+1;state=Status{};state.generation=generation;state.ap_ip=ap;
  buffers=static_cast<Buffers*>(heap_caps_malloc(sizeof(Buffers),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!buffers){state.error=Error::Allocation;return false;}new(buffers) Buffers{};
  state.resources_retained=true;state.phase=Phase::Starting;started=step_at=millis();window=request.window_ms;
  step=Step::PreBarrier;cleanup_fault=stop_issued=ap_touched=napt_attempted=false;need_ap_stop=false;
  std::memcpy(ap_name,request.ap_name.data(),request.ap_name.size());ap_name[request.ap_name.size()]=0;
  std::memcpy(ap_password,request.ap_password.data(),request.ap_password.size());ap_password[request.ap_password.size()]=0;
  events.enabled=true;events.ap_started=false;events.ap_stopped=false;
  auto err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,nullptr,&wifi_handler);
  if(err==ESP_OK)err=esp_event_handler_instance_register(CSP_OWNED_GUEST_EVENTS,BarrierId,event,nullptr,&barrier_handler);
  if(err!=ESP_OK){beginStop(Error::EventRegister,err);return false;}
  if(!postBarrier()){beginStop(Error::EventBarrier,state.native_error);return false;}
  return true;
}
void poll(uint32_t now){
  if(!owns())return;
  if(state.phase==Phase::Stopping){stopping(now);return;}
  if(state.phase==Phase::Blocked||state.phase==Phase::Stopped)return;
  if(!currentUplink()){beginStop(Error::UplinkChanged);return;}
  if(elapsed(now,started,window)){beginStop(Error::Expired);return;}
  if(state.phase==Phase::Starting){
    if(elapsed(now,step_at,TransitionTimeout)){beginStop(Error::Deadline);return;}
    if(step==Step::PreBarrier&&barrierSeen()){
      events.ap_started=false;events.ap_stopped=false;
      events.ap_epoch=0;events.ap_epoch_exhausted=false;admitted_stop_epoch=0;
      if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||!configure()){beginStop(Error::ApConfigure);return;}
      step=Step::EmptyClients;
    }
    if(step==Step::EmptyClients){
      wifi_sta_list_t clients{};const auto err=esp_wifi_ap_get_sta_list(&clients);
      if(err!=ESP_OK){beginStop(Error::ApProof,err);return;}
      if(clients.num)return;
      if(!postBarrier()){beginStop(Error::EventBarrier,state.native_error);return;}step=Step::ReadyBarrier;
    }
    if(step==Step::ReadyBarrier&&barrierSeen()){
      if(!apLive(true)){beginStop(Error::ApProof,state.native_error);return;}
      if(!udp(ap_socket,state.ap_ip,53)){beginStop(Error::Socket,errno);return;}
      if(!udp(up_socket,uplink.ip,0)){beginStop(Error::Socket,errno);return;}
      if(!apLive()){beginStop(Error::ApProof,state.native_error);return;}
      napt_attempted=true;const auto err=esp_netif_napt_enable(ap_netif);
      if(err!=ESP_OK){beginStop(Error::EnableNapt,err);return;}
      state.phase=Phase::Active;
    }
    return;
  }
  // AP health is independent of the preserved station uplink. The ordinary
  // stopping path retains ownership until NAPT/socket/AP/event cleanup proves
  // completion; expected AP_STOP callbacks during that path are not new faults.
  if(!apLive()){beginStop(Error::ApProof,state.native_error);return;}
  wifi_sta_list_t clients{};const auto err=esp_wifi_ap_get_sta_list(&clients);
  if(err!=ESP_OK||clients.num>1){beginStop(Error::ApProof,err);return;}
  dns(now);
}
bool stop(){
  if(!owns())return false;
  if(state.phase==Phase::Stopped)return true;
  if(state.phase!=Phase::Blocked)beginStop();
  return false;
}
Status status(){
  if(!owns()){Status denied;denied.phase=Phase::Blocked;denied.error=Error::Owner;return denied;}
  auto copy=state;const auto used=uint32_t(millis()-started);
  copy.remaining_ms=(copy.phase==Phase::Starting||copy.phase==Phase::Active)&&used<window?window-used:0;
  return copy;
}
#if CSP_PRODUCT_LAB6_PROFILE
Diagnostic diagnostic(){return owns()?diagnostic_state:Diagnostic{};}
#endif
} // namespace pro::owned_guest
#endif
