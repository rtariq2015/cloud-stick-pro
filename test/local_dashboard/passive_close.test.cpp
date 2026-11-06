// Actual production server and native loopback sockets. Clock, injected errno
// and allocation accounting are synthetic; no ESP/lwIP/USB/TLS claim is made.
#include "local_dashboard.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
using namespace pro::local_dashboard;
extern "C" int __real_close(int);
extern "C" ssize_t __real_recv(int,void*,size_t,int);
extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&) noexcept;
extern "C" void __real__ZdlPv(void*) noexcept;
namespace {
constexpr size_t BufferBytes=HeaderLimit+1+640+StatusLimit;
void* working=nullptr;
unsigned liveBuffers=0,allocations=0,deletions=0,peeks=0;
int peekError=0,closeError=0,releasedFd=-1;
unsigned closes=0;
uint32_t now=1000;
Snapshot state;
uint32_t ip(){return inet_addr("127.0.0.1");}
void poll(Server& server){server.poll(now,true,ip(),true,state);}
void tick(Server& server){poll(server);++now;}
uint16_t unusedPort(){
  const int fd=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(fd>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=ip();
  assert(::bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
  socklen_t size=sizeof address;assert(::getsockname(fd,reinterpret_cast<sockaddr*>(&address),&size)==0);
  const auto port=ntohs(address.sin_port);assert(__real_close(fd)==0);return port;
}
int connectClient(uint16_t port){
  const int fd=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(fd>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=ip();address.sin_port=htons(port);
  assert(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
  int on=1;assert(::ioctl(fd,FIONBIO,&on)==0);return fd;
}
struct Response {int fd=-1;std::string bytes;uint32_t finished=0,accepted=0;};
size_t responseLength(const std::string& bytes){
  const auto end=bytes.find("\r\n\r\n");if(end==std::string::npos)return 0;
  const auto start=bytes.find("Content-Length: ");assert(start!=std::string::npos&&start<end);
  const auto finish=bytes.find("\r\n",start);assert(finish!=std::string::npos);
  const std::string number=bytes.substr(start+16,finish-start-16);
  assert(!number.empty()&&number.find_first_not_of("0123456789")==std::string::npos);
  return end+4+std::stoul(number);
}
Response complete(Server& server,uint16_t port,uint32_t start=0){
  if(start)now=start;Response result;result.fd=connectClient(port);result.accepted=now;
  const std::string request="GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
  assert(::send(result.fd,request.data(),request.size(),0)==ssize_t(request.size()));
  for(unsigned turn=0;turn<2000;++turn){
    const auto before=liveBuffers;tick(server);if(before==1&&!liveBuffers)result.finished=now-1;
    char bytes[2048];const auto count=::recv(result.fd,bytes,sizeof bytes,0);
    if(count>0)result.bytes.append(bytes,size_t(count));else assert(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK));
    const auto length=responseLength(result.bytes);
    if(length&&result.bytes.size()==length)break;
    assert(!length||result.bytes.size()<length);usleep(100);
  }
  assert(!result.bytes.empty()&&responseLength(result.bytes)==result.bytes.size());
  const auto separator=result.bytes.find("\r\n\r\n");
  char expected[StatusLimit]{};const auto size=json(state,expected,sizeof expected);assert(size>0);
  assert(result.bytes.substr(separator+4)==std::string(expected,size));
  assert(result.bytes.find("HTTP/1.1 200 OK\r\n")==0);
  assert(result.bytes.find("Content-Security-Policy: ")!=std::string::npos);
  // Completed caller-owned response buffers are gone in both variants. Only
  // the flagged path may retain its descriptor to give the peer FIN priority.
  assert(liveBuffers==0&&working==nullptr);
  assert(server.serving()==bool(CSP_DASHBOARD_PASSIVE_CLOSE));return result;
}
void peerFin(Server& server,Response& response){
  assert(__real_close(response.fd)==0);response.fd=-1;
  for(unsigned turn=0;turn<20&&server.serving();++turn){tick(server);usleep(100);}
  assert(!server.serving()&&!server.faulted());
}
struct WaitCounts {unsigned local=0,remote=0;bool available=false;};
WaitCounts timeWait(uint16_t port){
  std::ifstream input("/proc/net/tcp");WaitCounts result;if(!input)return result;
  result.available=true;std::string line;std::getline(input,line);
  while(std::getline(input,line)){
    std::istringstream row(line);std::string slot,local,remote,status;
    if(!(row>>slot>>local>>remote>>status)||status!="06")continue;
    const auto left=local.find(':'),right=remote.find(':');if(left==std::string::npos||right==std::string::npos)continue;
    if(std::stoul(local.substr(left+1),nullptr,16)==port)++result.local;
    if(std::stoul(remote.substr(right+1),nullptr,16)==port)++result.remote;
  }
  return result;
}
}
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t size,const std::nothrow_t& tag) noexcept{
  void* result=__real__ZnwmRKSt9nothrow_t(size,tag);
  if(size==BufferBytes&&result){assert(!working);working=result;++liveBuffers;++allocations;}
  return result;
}
extern "C" void __wrap__ZdlPv(void* pointer) noexcept{
  if(pointer&&pointer==working){working=nullptr;assert(liveBuffers==1);--liveBuffers;++deletions;}
  __real__ZdlPv(pointer);
}
extern "C" ssize_t __wrap_recv(int fd,void* output,size_t size,int flags){
  if(flags&MSG_PEEK){
    assert(CSP_DASHBOARD_PASSIVE_CLOSE&&size==1);assert((::fcntl(fd,F_GETFL)&O_NONBLOCK)!=0);++peeks;
    if(peekError){errno=peekError;return -1;}
  }
  return __real_recv(fd,output,size,flags);
}
extern "C" int __wrap_close(int fd){
  ++closes;if(closeError){releasedFd=fd;assert(__real_close(fd)==0);errno=closeError;return -1;}
  return __real_close(fd);
}
int main(){
  ::signal(SIGPIPE,SIG_IGN);state.firmware="0.4.0-dashboard1";state.verified=4;state.has_scan=true;state.scan_complete=true;
  const uint16_t port=unusedPort();Server server{port};tick(server);assert(server.listening());
  const unsigned startedAllocations=allocations;
  // Content-Length-first clients close only after independently counting every
  // header/body byte. Kernel TIME_WAIT observations are native-Linux evidence.
  for(unsigned i=0;i<24;++i){auto response=complete(server,port);if(!i)std::printf("NATIVE_STATUS_JSON %s\n",response.bytes.substr(response.bytes.find("\r\n\r\n")+4).c_str());peerFin(server,response);}
  assert(allocations-startedAllocations==24&&liveBuffers==0);
  usleep(20000);const auto waiting=timeWait(port);
  std::printf("NATIVE_TIME_WAIT passive=%d local_port=%u remote_port=%u available=%s\n",CSP_DASHBOARD_PASSIVE_CLOSE,waiting.local,waiting.remote,waiting.available?"true":"false");
  // EOF-waiting clients cannot deadlock with the passive grace interval. Wait
  // exactly to its boundary using the wrap-safe synthetic owner-loop clock.
  auto eof=complete(server,port);
#if CSP_DASHBOARD_PASSIVE_CLOSE
  now=eof.finished+249;const auto before=peeks;poll(server);assert(server.serving()&&peeks==before+1);
  now=eof.finished+250;poll(server);assert(!server.serving()&&peeks==before+1);
#endif
  char byte=0;bool observedEof=false;
  for(unsigned turn=0;turn<20&&!observedEof;++turn){const auto count=::recv(eof.fd,&byte,1,0);if(!count)observedEof=true;else assert(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK));usleep(100);}
  assert(observedEof&&__real_close(eof.fd)==0);
#if CSP_DASHBOARD_PASSIVE_CLOSE
  // The original accepted+3s deadline wins even when less than250 ms remains
  // after the final response byte, and both clocks are safe across uint32 wrap.
  auto wrap=complete(server,port,UINT32_MAX-2);now=wrap.finished+249;poll(server);assert(server.serving());now=wrap.finished+250;poll(server);assert(!server.serving());assert(__real_close(wrap.fd)==0);
  int delayed=connectClient(port);const auto accepted=now;tick(server);now=accepted+2750;
  const std::string request="GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";assert(::send(delayed,request.data(),request.size(),0)==ssize_t(request.size()));
  std::string delayedBytes;
  for(unsigned turn=0;turn<100&&server.serving();++turn){tick(server);char bytes[2048];const auto n=::recv(delayed,bytes,sizeof bytes,0);if(n>0)delayedBytes.append(bytes,size_t(n));else assert(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK));if(responseLength(delayedBytes)==delayedBytes.size()&&!delayedBytes.empty())break;}
  assert(responseLength(delayedBytes)==delayedBytes.size()&&server.serving());now=accepted+3000;poll(server);assert(!server.serving());assert(__real_close(delayed)==0);
  // One peek per poll; EAGAIN/EINTR never spins, while a terminal errno or
  // unexpected pipelined byte closes rather than reopening request parsing.
  for(const int error:{EAGAIN,EINTR,EIO}){
    auto response=complete(server,port);const auto beforePeek=peeks;peekError=error;poll(server);peekError=0;assert(peeks==beforePeek+1);
    assert(server.serving()==(error!=EIO));if(server.serving())peerFin(server,response);else assert(__real_close(response.fd)==0);
  }
  auto extra=complete(server,port);assert(::send(extra.fd,"X",1,0)==1);poll(server);assert(!server.serving());assert(__real_close(extra.fd)==0);
  // Every maintenance/memory/network transition immediately abandons grace.
  for(unsigned reason=0;reason<5;++reason){
    auto response=complete(server,port);const auto beforePeek=peeks;
    if(reason==0)assert(server.stop());
    if(reason==1)server.poll(now,false,ip(),true,state);
    if(reason==2)server.poll(now,true,ip(),false,state);
    if(reason==3)server.poll(now,true,0,true,state);
    if(reason==4)server.poll(now,true,inet_addr("127.0.0.2"),true,state);
    assert(!server.serving()&&server.listening()==(reason==4)&&peeks==beforePeek);assert(__real_close(response.fd)==0);assert(server.stop());tick(server);
  }
  // An ambiguous close after completion still latches terminal ownership and
  // must never retry an FD reused by another native socket. No caller buffer
  // remains borrowed or retained at this stage.
  assert(server.stop());Server fault{port};tick(fault);auto broken=complete(fault,port);closeError=EIO;assert(!fault.stop());closeError=0;
  const int recycled=releasedFd;const int candidate=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(candidate>=0);
  if(candidate!=recycled){assert(::dup2(candidate,recycled)==recycled);assert(__real_close(candidate)==0);}
  const auto beforeClose=closes;assert(fault.faulted()&&!fault.stop());fault.poll(now,true,ip(),true,state);assert(closes==beforeClose&&::fcntl(recycled,F_GETFD)>=0);
  assert(liveBuffers==0);assert(__real_close(recycled)==0);assert(__real_close(broken.fd)==0);
#endif
  assert(server.stop());assert(liveBuffers==0&&allocations==deletions);
  std::printf("PASS passive=%d sizeof_server=%zu caller_buffer_bytes=%zu allocations=%u deletions=%u peeks=%u: exact native framing, FIN/EOF/deadline/wrap/error/shutdown and no-retry ownership\n",CSP_DASHBOARD_PASSIVE_CLOSE,sizeof(Server),BufferBytes,allocations,deletions,peeks);
}
