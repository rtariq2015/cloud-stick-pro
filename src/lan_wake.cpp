// SPDX-License-Identifier: MIT
#include "ble_feature.h"
#include "owner_login_feature.h"
#include "local_dashboard.h"
#include "lan_wake.h"
#if CSP_ENABLE_BLE_WIFI_WAKE
#include "ble_wake_runtime.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <lwip/sockets.h>
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
namespace pro::ble_wake {
struct Server::Buffers {char request[1537]{},response[512]{};Frame frame;};
namespace {
Server server;
bool nonblocking(int fd){int on=1;return ::ioctl(fd,FIONBIO,&on)==0;}
bool again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;}
bool closeFd(int& fd){
  if(fd<0)return true;
  // Never retry an ambiguous descriptor which another task could have reused.
  if(::close(fd)!=0&&errno!=EBADF)return false;
  fd=-1;return true;
}
template<size_t N>void hex(const std::array<uint8_t,N>& input,char(&out)[N*2+1]){
  constexpr char digits[]="0123456789abcdef";for(size_t i=0;i<N;++i){out[2*i]=digits[input[i]>>4];out[2*i+1]=digits[input[i]&15];}out[N*2]=0;
}
}
Server::~Server(){if(!fault_)stop();delete buffers_;}
bool Server::closeClient(){
  if(!closeFd(client_)){fault_=true;return false;}
  delete buffers_;buffers_=nullptr;received_=header_=total_=sent_=0;parsed_=sending_=waiting_=false;return true;
}
bool Server::stop(){
  if(fault_)return false;
  const bool client=closeClient(),listener=closeFd(listener_);if(!client||!listener){fault_=true;return false;}
  address_=0;grant_=ready_=false;return true;
}
bool Server::start(uint32_t address){
  listener_=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_<0)return false;
  int reuse=1,limit=2048;
  if(::setsockopt(listener_,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)!=0||::setsockopt(listener_,SOL_SOCKET,SO_RCVBUF,&limit,sizeof limit)!=0||!nonblocking(listener_)){stop();return false;}
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(port_);local.sin_addr.s_addr=address;
  if(::bind(listener_,reinterpret_cast<sockaddr*>(&local),sizeof local)!=0||::listen(listener_,1)!=0){stop();return false;}
  const auto* b=reinterpret_cast<const unsigned char*>(&address);std::snprintf(host_,sizeof host_,"%u.%u.%u.%u:%u",b[0],b[1],b[2],b[3],unsigned(port_));address_=address;return true;
}
void Server::respond(int code,const char* body){
  const int length=std::snprintf(buffers_->response,sizeof buffers_->response,
    "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; frame-ancestors 'none'\r\n\r\n%s",
    code,code==200?"OK":code==403?"Forbidden":code==409?"Conflict":code==503?"Unavailable":"Bad Request",unsigned(std::strlen(body)),body);
  if(length<=0||size_t(length)>=sizeof buffers_->response){grant_=false;closeClient();return;}
  total_=size_t(length);sent_=0;sending_=true;
}
void Server::dispatch(uint32_t now,bool available){
  const auto& frame=buffers_->frame;
  if(frame.route==Route::Challenge){
    Nonce nonce;const auto state=challenge(now,available,nonce);
    if(state!=Result::Ready){respond(state==Result::Busy?409:403,"{\"v\":1,\"state\":\"unavailable\"}");return;}
    char id[IdBytes*2+1],random[NonceBytes*2+1],body[160];hex(identity(),id);hex(nonce,random);
    std::snprintf(body,sizeof body,"{\"v\":1,\"id\":\"%s\",\"nonce\":\"%s\"}",id,random);respond(200,body);return;
  }
  Id id;Nonce nonce;std::array<uint8_t,72> signature;size_t size=0;
  if(!proof(buffers_->request+header_,frame.body,id,nonce,signature,size)||id!=identity()){respond(403,"{\"v\":1,\"state\":\"denied\"}");return;}
  const auto state=authorize(now,available,nonce,signature.data(),size);
  if(state!=Result::Ready){respond(state==Result::Busy?409:403,"{\"v\":1,\"state\":\"denied\"}");return;}
  grant_=true;respond(200,"{\"v\":1,\"state\":\"opening\"}");
}
bool Server::takeRequest(){
  if(!ready_||fault_)return false;
  // The reply and passive peer close precede listener/working-buffer release.
  // Only then may the owner pause its worker and enter the existing BLE policy.
  return stop();
}
void Server::poll(uint32_t now,bool allowed,uint32_t address,bool available){
  if(fault_)return;
  if(!allowed||!address){stop();return;}
  if(ready_)return;
  if(listener_>=0&&address_!=address&&!stop())return;
  if(listener_<0){if(retry_&&int32_t(now-retry_)<0)return;if(!start(address)){retry_=now+5000;return;}}
  if(client_<0){
    client_=::accept(listener_,nullptr,nullptr);if(client_<0)return;
    int limit=2048;if(!nonblocking(client_)||::setsockopt(client_,SOL_SOCKET,SO_RCVBUF,&limit,sizeof limit)!=0){closeClient();return;}
    buffers_=new(std::nothrow) Buffers;if(!buffers_){closeClient();return;}accepted_=now;grant_=false;
  }
  if(uint32_t(now-accepted_)>=3000){grant_=false;closeClient();return;}
  if(waiting_){
    char extra=0;const int n=::recv(client_,&extra,1,MSG_PEEK);
    if(n==0){const bool grant=grant_;if(closeClient()&&grant)ready_=true;return;}
    if(n>0||(n<0&&!again())||uint32_t(now-finished_)>=500){grant_=false;closeClient();}return;
  }
  if(!sending_){
    const size_t room=1536-received_;if(!room){closeClient();return;}
    const int n=::recv(client_,buffers_->request+received_,std::min(room,size_t(256)),0);
    if(n<0){if(!again())closeClient();return;}if(n==0){closeClient();return;}
    received_+=size_t(n);buffers_->request[received_]=0;
    if(!parsed_){
      const char* end=std::strstr(buffers_->request,"\r\n\r\n");if(!end)return;header_=size_t(end-buffers_->request)+4;
      if(!request(buffers_->request,header_,host_,buffers_->frame)){respond(400,"{\"v\":1,\"state\":\"invalid\"}");return;}parsed_=true;
    }
    const size_t required=header_+buffers_->frame.body;
    if(received_>required){respond(400,"{\"v\":1,\"state\":\"invalid\"}");return;}
    if(received_==required)dispatch(now,available);
    return;
  }
  const int n=::send(client_,buffers_->response+sent_,std::min(total_-sent_,size_t(256)),0);
  if(n<0){if(!again()){grant_=false;closeClient();}return;}if(n==0){grant_=false;closeClient();return;}
  sent_+=size_t(n);if(sent_==total_){waiting_=true;finished_=now;}
}
void poll(uint32_t now,bool allowed,uint32_t address,bool available){server.poll(now,allowed,address,available);}
bool takeRequest(){return server.takeRequest();}
bool stop(){return server.stop();}
}
#endif
