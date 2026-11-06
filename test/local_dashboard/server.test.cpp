#include "local_dashboard.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <new>
using namespace pro::local_dashboard;
extern "C" int __real_close(int);
static bool fail_close=false,release_then_fail=false;
static int ambiguous_fd=-1;
static bool fail_new=false;
extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&) noexcept;
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t& tag) noexcept {
  if(fail_new)return nullptr;
  return __real__ZnwmRKSt9nothrow_t(n,tag);
}
extern "C" int __wrap_close(int fd){if(fail_close){ambiguous_fd=fd;if(release_then_fail)assert(__real_close(fd)==0);errno=EIO;return -1;}return __real_close(fd);}
static int port=0;
static uint32_t clock_ms=1000;
static Snapshot state;
static uint32_t ip(){return inet_addr("127.0.0.1");}
static void tick(Server& s,unsigned count=1){for(unsigned i=0;i<count;++i){s.poll(clock_ms++,true,ip(),true,state);}}
static int client(){int fd=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(fd>=0);sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(port);target.sin_addr.s_addr=ip();assert(::connect(fd,reinterpret_cast<sockaddr*>(&target),sizeof target)==0);int on=1;assert(::ioctl(fd,FIONBIO,&on)==0);return fd;}
static std::string exchange(Server& server,const std::string& request,unsigned split=0){
  int fd=client();size_t offset=0;std::string response;bool eof=false;
  for(unsigned turns=0;turns<2000&&!eof;++turns){
    if(offset<request.size()){
      size_t length=split?std::min(size_t(split),request.size()-offset):request.size()-offset;
      const int n=::send(fd,request.data()+offset,length,0);if(n>0)offset+=size_t(n);else assert(errno==EAGAIN||errno==EWOULDBLOCK);
    }
    tick(server);char bytes[4096];const int n=::recv(fd,bytes,sizeof bytes,0);
    if(n>0)response.append(bytes,size_t(n));else if(n==0)eof=true;else assert(errno==EAGAIN||errno==EWOULDBLOCK||errno==ECONNRESET);
    // Allow the real loopback stack scheduling/ACKs; this is a desktop socket
    // test, not claimed ESP allocation, latency or USB acceptance.
    usleep(100);
  }
  assert(eof);assert(__real_close(fd)==0);return response;
}
int main(){
  signal(SIGPIPE,SIG_IGN);state.firmware="0.4.0-dashboard1";state.verified=4;state.has_scan=true;
  // Reserve/release an ephemeral loopback port for the exact production class.
  int probe=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(probe>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=ip();assert(::bind(probe,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);socklen_t length=sizeof address;assert(::getsockname(probe,reinterpret_cast<sockaddr*>(&address),&length)==0);port=ntohs(address.sin_port);assert(__real_close(probe)==0);
  Server server{uint16_t(port)};tick(server);assert(server.listening());
  for(const auto* path:{"/","/app.js","/status"}){
    const auto response=exchange(server,std::string("GET ")+path+" HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",1);
    assert(response.find("HTTP/1.1 200 OK\r\n")==0);assert(response.find("Cache-Control: no-store\r\n")!=std::string::npos);
    assert(response.find("frame-ancestors 'none'")!=std::string::npos);assert(response.find("Access-Control-Allow-Origin")==std::string::npos);
    const auto separator=response.find("\r\n\r\n");assert(separator!=std::string::npos);
    const auto start=response.find("Content-Length: ");assert(start!=std::string::npos);
    const auto bytes=std::stoul(response.substr(start+16));assert(bytes==response.size()-separator-4);
    if(std::strcmp(path,"/status")==0)assert(response.find("\"verified\":4")!=std::string::npos);
  }
  assert(exchange(server,"POST /config HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").find("HTTP/1.1 400")==0);
  assert(exchange(server,"GET /status HTTP/1.1\r\nHost: evil.example\r\n\r\n").find("HTTP/1.1 403")==0);
  assert(exchange(server,"GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\nEXTRA").find("HTTP/1.1 400")==0);
  int fd=client();tick(server);assert(server.serving());clock_ms+=3000;tick(server);assert(!server.serving());assert(__real_close(fd)==0);
  // Memory pressure, ownership transitions and address changes all close both
  // active client and listening PCB rather than waiting for an HTTP timeout.
  fd=client();tick(server);server.poll(clock_ms++,true,ip(),false,state);assert(!server.serving()&&!server.listening());assert(__real_close(fd)==0);
  tick(server);fd=client();tick(server);server.poll(clock_ms++,false,ip(),true,state);assert(!server.serving()&&!server.listening());assert(__real_close(fd)==0);
  tick(server);fd=client();fail_new=true;tick(server);assert(!server.serving());fail_new=false;assert(__real_close(fd)==0);
  for(unsigned i=0;i<100;++i){tick(server);assert(server.listening());assert(server.stop());assert(!server.listening());}
  assert(server.stop());
  // Model VFS close releasing the numeric descriptor before returning an error.
  // The backup/TLS task can then acquire the same number. Cleanup must NEVER
  // retry that stale integer or falsely admit BLE/new listener work.
  Server faulty{uint16_t(port)};tick(faulty);fd=client();tick(faulty);
  fail_close=true;release_then_fail=true;assert(!faulty.stop());assert(faulty.faulted());const int released=ambiguous_fd;
  fail_close=false;const int reused=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(reused>=0);
  // Ensure the tested descriptor is the exact recycled integer, even if the
  // platform chooses another freed slot first.
  if(reused!=released){assert(::dup2(reused,released)==released);assert(__real_close(reused)==0);}
  assert(!faulty.stop());faulty.poll(clock_ms++,true,ip(),true,state);assert(!faulty.listening());
  assert(::fcntl(released,F_GETFD)>=0);assert(__real_close(released)==0);assert(__real_close(fd)==0);
  std::puts("PASS real fragmented loopback HTTP, exact lengths, denied mutations/hosts/body, finite lifetime, memory/ownership close, allocation failure, 100 starts/stops, terminal ambiguous-close/fd-reuse barrier");
}
