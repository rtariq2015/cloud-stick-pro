#include "ble_feature.h"
#include "local_dashboard.h"
#if CSP_ENABLE_LOCAL_DASHBOARD
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
namespace pro::local_dashboard {
struct Server::Buffers {
  char request[HeaderLimit+1]{},header[640]{},status[StatusLimit]{};
};
namespace {
bool nonblocking(int fd){int on=1;return ::ioctl(fd,FIONBIO,&on)==0;}
bool again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;}
bool closeFd(int& fd){
  if(fd<0)return true;
  // This descriptor belongs exclusively to the owner loop. EBADF proves it
  // no longer exists. Other errors have ambiguous release semantics: callers
  // latch a terminal fault and NEVER retry that integer, which another task
  // could already have reused for its TLS socket.
  if(::close(fd)!=0&&errno!=EBADF)return false;
  fd=-1;return true;
}
Server server;
}
Server::~Server(){
  // Caller-owned HTTP bytes are copied by BSD send; the socket never borrows
  // these application arrays. An ambiguous descriptor is never retried, even
  // during destruction. The normal firmware instance has app lifetime.
  if(!cleanup_uncertain_)stop();
  delete buffers_;
}
bool Server::closeClient(){
  if(!closeFd(client_)){cleanup_uncertain_=true;return false;}
  delete buffers_;buffers_=nullptr;
  sending_=false;body_=nullptr;
  received_=header_size_=header_sent_=body_size_=body_sent_=0;
#if CSP_DASHBOARD_PASSIVE_CLOSE
  waiting_for_peer_=false;finished_=0;
#endif
  return true;
}
bool Server::stop(){
  if(cleanup_uncertain_)return false;
  const bool client_closed=closeClient(),listener_closed=closeFd(listener_);
  if(!client_closed||!listener_closed){cleanup_uncertain_=true;return false;}
  address_=0;retry_at_=0;return true;
}
bool Server::start(uint32_t address){
  listener_=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_<0)return false;
  const int reuse=1,receive_limit=2048;
  if(::setsockopt(listener_,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)!=0||
     ::setsockopt(listener_,SOL_SOCKET,SO_RCVBUF,&receive_limit,sizeof receive_limit)!=0||!nonblocking(listener_)){
    stop();return false;
  }
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(port_);local.sin_addr.s_addr=address;
  // Exact STA address, never INADDR_ANY or the temporary public-login AP.
  if(::bind(listener_,reinterpret_cast<sockaddr*>(&local),sizeof local)!=0||::listen(listener_,1)!=0){stop();return false;}
  const auto* bytes=reinterpret_cast<const unsigned char*>(&address);
  std::snprintf(host_,sizeof host_,"%u.%u.%u.%u",bytes[0],bytes[1],bytes[2],bytes[3]);
  address_=address;return true;
}
bool Server::response(Route target,const Snapshot& snapshot){
  const char* mime="text/plain; charset=utf-8";int code=200;const char* reason="OK";
  switch(target){
    case Route::Page:body_=Page;body_size_=PageSize;mime="text/html; charset=utf-8";break;
    case Route::Script:body_=Script;body_size_=ScriptSize;mime="text/javascript; charset=utf-8";break;
    case Route::Status:body_size_=json(snapshot,buffers_->status,sizeof buffers_->status);body_=buffers_->status;mime="application/json; charset=utf-8";if(!body_size_)return false;break;
    default:code=target==Route::Forbidden?403:400;reason=code==403?"Forbidden":"Bad Request";body_="Open the station IP shown on Cloud Stick Pro.\n";body_size_=std::strlen(body_);break;
  }
  const int n=std::snprintf(buffers_->header,sizeof buffers_->header,
    "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nPermissions-Policy: bluetooth=(), camera=(), microphone=(), geolocation=()\r\n"
    "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'\r\n\r\n",
    code,reason,mime,unsigned(body_size_));
  if(n<=0||size_t(n)>=sizeof buffers_->header)return false;
  header_size_=size_t(n);sending_=true;return true;
}
void Server::poll(uint32_t now,bool allowed,uint32_t address,bool memory_ok,const Snapshot& snapshot){
  if(cleanup_uncertain_)return;
  if(!allowed||!address||!memory_ok){stop();return;}
  if(listener_>=0&&address_!=address&&!stop())return;
  if(listener_<0){
    if(retry_at_&&int32_t(now-retry_at_)<0)return;
    if(!start(address)){retry_at_=now+5000;return;}
  }
  if(client_<0){
    client_=::accept(listener_,nullptr,nullptr);
    if(client_<0)return;
    const int receive_limit=2048;
    if(!nonblocking(client_)||::setsockopt(client_,SOL_SOCKET,SO_RCVBUF,&receive_limit,sizeof receive_limit)!=0){closeClient();return;}
    buffers_=new(std::nothrow) Buffers;
    if(!buffers_){closeClient();return;}
    accepted_=now;
  }
  // Total lifetime, not renewed by trickled input/output. Wrap-safe and finite.
  if(uint32_t(now-accepted_)>=3000){closeClient();return;}
#if CSP_DASHBOARD_PASSIVE_CLOSE
  if(waiting_for_peer_){
    // All Content-Length bytes were accepted by BSD send. The application
    // buffers were released then; only this owner-loop descriptor remains.
    // A Content-Length-aware peer can close first. EOF-waiting clients still
    // receive a normal close after at most 250 ms, within the original 3 s cap.
    if(uint32_t(now-finished_)>=250){closeClient();return;}
    char extra=0;
    const int n=::recv(client_,&extra,1,MSG_PEEK);
    if(n>=0||!again())closeClient(); // FIN, unexpected bytes or terminal error.
    return;
  }
#endif
  if(!sending_){
    const size_t room=HeaderLimit-received_;
    if(!room){closeClient();return;}
    const int n=::recv(client_,buffers_->request+received_,std::min(room,size_t(256)),0);
    if(n<0){if(!again())closeClient();return;}if(n==0){closeClient();return;}
    received_+=size_t(n);buffers_->request[received_]=0;
    const char* end=std::strstr(buffers_->request,"\r\n\r\n");
    if(end){
      const size_t size=size_t(end-buffers_->request)+4;
      const auto target=size==received_?route(buffers_->request,size,host_):Route::BadRequest;
      if(!response(target,snapshot))closeClient();
    }else if(received_==HeaderLimit)closeClient();
    return;
  }
  const bool header_pending=header_sent_<header_size_;
  const char* bytes=header_pending?buffers_->header+header_sent_:body_+body_sent_;
  const size_t left=header_pending?header_size_-header_sent_:body_size_-body_sent_;
  // A raw nonblocking send keeps HTTP work away from Arduino's blocking
  // Stream timeout paths. At most 512 bytes enter TCP per owner-loop iteration.
  const int n=::send(client_,bytes,std::min(left,size_t(512)),0);
  if(n<0){if(!again())closeClient();return;}if(n==0){closeClient();return;}
  if(header_pending)header_sent_+=size_t(n);else body_sent_+=size_t(n);
  if(header_sent_==header_size_&&body_sent_==body_size_){
#if CSP_DASHBOARD_PASSIVE_CLOSE
    // BSD send copies these arrays; TCP never borrows their backing storage.
    // Release only caller-owned memory immediately, before waiting for FIN.
    delete buffers_;buffers_=nullptr;body_=nullptr;sending_=false;
    received_=header_size_=header_sent_=body_size_=body_sent_=0;
    finished_=now;waiting_for_peer_=true;
#else
    closeClient();
#endif
  }
}
void poll(uint32_t now,bool allowed,uint32_t address,bool memory_ok,const Snapshot& snapshot){server.poll(now,allowed,address,memory_ok,snapshot);}
bool stop(){return server.stop();}
bool listening(){return server.listening();}
bool faulted(){return server.faulted();}
}
#endif
