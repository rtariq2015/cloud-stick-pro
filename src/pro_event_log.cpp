#include "pro_event_log.h"
#if CSP_ENABLE_EVENT_LOG
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#ifndef ARDUINO
#include <mutex>
#include <fcntl.h>
#else
#include <Arduino.h>
#endif
#ifndef CSP_EVENT_LOG_ROOT
#define CSP_EVENT_LOG_ROOT "/littlefs"
#endif
// Test-only compile seams exercise actual bounded POSIX I/O, including errors.
#ifndef CSP_EVENT_OPEN
#define CSP_EVENT_OPEN std::fopen
#define CSP_EVENT_READ std::fread
#define CSP_EVENT_ERROR std::ferror
#define CSP_EVENT_WRITE std::fwrite
#define CSP_EVENT_FLUSH std::fflush
#define CSP_EVENT_SYNC ::fsync
#define CSP_EVENT_CLOSE std::fclose
#define CSP_EVENT_RENAME std::rename
#endif
namespace pro::events {
namespace {
struct State {
  Event ring[Capacity]{};
  uint32_t newest=0,persisted=0,attempted=0,attempted_at=0;
  uint8_t head=0,count=0;
  bool loaded=false,dirty=false,corrupt=false,io=false,exhausted=false,writable=false;
};
State state;
#ifdef ARDUINO
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
struct Lock {Lock(){portENTER_CRITICAL(&mux);}~Lock(){portEXIT_CRITICAL(&mux);}};
#else
std::mutex mux;
using Lock=std::lock_guard<std::mutex>;
#endif
static_assert(sizeof(State)+sizeof(mux)<=512,"Event log static RAM budget exceeded");
static_assert(MaximumSnapshotBytes<=4096,"Event snapshot storage budget exceeded");
constexpr char Final[]=CSP_EVENT_LOG_ROOT "/events.log",Temp[]=CSP_EVENT_LOG_ROOT "/events.tmp";
constexpr uint8_t Magic[]={67,83,80,69,86,84,1,0};
constexpr uint64_t FirstValidUTC=1767225600,LastValidUTC=253402300799;
#ifdef ARDUINO
#define EVENT_LOCK Lock lock
#else
#define EVENT_LOCK Lock lock(mux)
#endif
uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint64_t get64(const uint8_t* p){return uint64_t(get32(p))|(uint64_t(get32(p+4))<<32);}
void put32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
void put64(uint8_t* p,uint64_t n){put32(p,uint32_t(n));put32(p+4,uint32_t(n>>32));}
uint32_t crc(const uint8_t* p,size_t n,uint32_t value=0xffffffffu){
  for(size_t i=0;i<n;++i){value^=p[i];for(unsigned j=0;j<8;++j)value=(value>>1)^(0xedb88320u&uint32_t(0-int32_t(value&1)));}
  return value;
}
uint32_t checksum(const uint8_t* p,size_t n){return ~crc(p+WireHeaderBytes,n-WireHeaderBytes,crc(p,16));}
bool kindValid(Kind k){return unsigned(k)>=unsigned(Kind::Boot)&&unsigned(k)<=unsigned(Kind::CloudCheck);}
bool eventValid(const Event& event){
  return event.sequence&&kindValid(event.kind)&&(!event.utc_unix||(event.utc_unix>=FirstValidUTC&&event.utc_unix<=LastValidUTC))&&
    (event.kind!=Kind::VerifiedCount||(event.code>=0&&event.code<=128));
}
void encodeEvent(const Event& event,uint8_t* p){
  std::memset(p,0,WireRecordBytes);put32(p,event.sequence);put32(p+4,event.uptime_ms);
  put64(p+8,event.utc_unix);put64(p+16,event.bytes);put32(p+24,uint32_t(event.code));p[28]=uint8_t(event.kind);
}
bool decodeEvent(const uint8_t* p,Event& event){
  if(p[29]||p[30]||p[31])return false;
  event.sequence=get32(p);event.uptime_ms=get32(p+4);event.utc_unix=get64(p+8);event.bytes=get64(p+16);
  const uint32_t raw=get32(p+24);
  event.code=raw<=0x7fffffffu?int32_t(raw):-1-int32_t(~raw);
  event.kind=Kind(p[28]);return eventValid(event);
}
size_t encode(const Event* events,uint8_t count,uint32_t newest,uint8_t* wire){
  const size_t n=WireHeaderBytes+size_t(count)*WireRecordBytes;
  std::memset(wire,0,n);std::memcpy(wire,Magic,sizeof Magic);put32(wire+8,newest);wire[12]=count;wire[14]=WireRecordBytes;
  for(unsigned i=0;i<count;++i)encodeEvent(events[i],wire+WireHeaderBytes+i*WireRecordBytes);
  put32(wire+16,checksum(wire,n));return n;
}
bool decode(const uint8_t* wire,size_t n,Event* events,uint8_t& count,uint32_t& newest){
  if(n<WireHeaderBytes||n>MaximumSnapshotBytes||std::memcmp(wire,Magic,sizeof Magic)||wire[13]||wire[15]||wire[14]!=WireRecordBytes)return false;
  count=wire[12];newest=get32(wire+8);
  if(count>Capacity||n!=WireHeaderBytes+size_t(count)*WireRecordBytes||get32(wire+16)!=checksum(wire,n))return false;
  for(unsigned i=0;i<count;++i){
    if(!decodeEvent(wire+WireHeaderBytes+i*WireRecordBytes,events[i]))return false;
    // Sequence exhaustion stops new diagnostics. Never wrap/relabel history.
    if(i&&(events[i-1].sequence==0xffffffffu||events[i].sequence!=events[i-1].sequence+1))return false;
  }
  return count?newest==events[count-1].sequence:newest==0;
}
enum class Read {Missing,Valid,Corrupt,IOError};
Read readBytes(const char* path,uint8_t* wire,size_t& n){
  FILE* file=CSP_EVENT_OPEN(path,"rb");if(!file)return errno==ENOENT?Read::Missing:Read::IOError;
  std::setvbuf(file,nullptr,_IONBF,0);
  n=CSP_EVENT_READ(wire,1,MaximumSnapshotBytes+1,file);
  bool ok=!CSP_EVENT_ERROR(file);if(CSP_EVENT_CLOSE(file)!=0)ok=false;
  if(!ok)return Read::IOError;
  return n>MaximumSnapshotBytes?Read::Corrupt:Read::Valid;
}
Read read(const char* path,Event* events,uint8_t& count,uint32_t& newest){
  uint8_t wire[MaximumSnapshotBytes+1]{};size_t n=0;
  auto result=readBytes(path,wire,n);
  return result!=Read::Valid?result:decode(wire,n,events,count,newest)?Read::Valid:Read::Corrupt;
}
Status snapshot(){return {state.newest,state.persisted,state.count,state.loaded,state.dirty,state.corrupt,state.io,state.exhausted};}
} // namespace
bool record(Kind kind,int32_t code,uint64_t bytes,uint64_t utc,uint32_t uptime){
  if(!kindValid(kind)||(kind==Kind::VerifiedCount&&(code<0||code>128)))return false;
  if(utc<FirstValidUTC||utc>LastValidUTC)utc=0;
  EVENT_LOCK;
  if(state.newest==0xffffffffu){state.exhausted=true;return false;}
  if(kind!=Kind::Boot&&kind!=Kind::WorkerStart&&kind!=Kind::Retirement){
    for(unsigned i=0;i<state.count;++i){const auto& old=state.ring[(state.head+Capacity-1-i)%Capacity];
      if(old.kind==kind){if(old.code==code&&old.bytes==bytes)return false;break;}}
  }
  auto& event=state.ring[state.head];event={++state.newest,uptime,utc,bytes,code,kind};
  state.head=uint8_t((state.head+1)%Capacity);if(state.count<Capacity)++state.count;state.dirty=true;return true;
}
Status status(){EVENT_LOCK;return snapshot();}
Page readPage(uint32_t after){
  Page page;EVENT_LOCK;page.status=snapshot();
  for(unsigned i=0;i<state.count;++i){const auto& event=state.ring[(state.head+Capacity-state.count+i)%Capacity];
    if(event.sequence<=after)continue;
    if(page.count==PageCapacity){page.more=true;break;}
    page.events[page.count++]=event;page.next=event.sequence;
  }
  return page;
}
Load workerLoad(){
  {EVENT_LOCK;if(state.loaded)return Load::AlreadyLoaded;state.loaded=true;}
  Event events[Capacity]{};uint8_t count=0;uint32_t newest=0;
  const auto final=read(Final,events,count,newest);
  // An unreadable published snapshot has unknown validity. Do not replace it
  // merely because a leftover temporary happens to be readable.
  if(final==Read::IOError){EVENT_LOCK;state.io=true;return Load::IOError;}
  auto temp=Read::Missing;
  if(final!=Read::Valid)temp=read(Temp,events,count,newest);
  const bool recovered=final!=Read::Valid&&temp==Read::Valid;
  EVENT_LOCK;
  if(final==Read::Valid||recovered){
    // Loading is startup-only: any preexisting RAM facts are retained rather
    // than silently overwritten. The normal worker loads before its first event.
    if(state.count){state.io=true;return Load::IOError;}
    for(unsigned i=0;i<count;++i)state.ring[i]=events[i];state.count=count;state.head=count%Capacity;state.newest=newest;
    state.persisted=recovered?0:newest;state.dirty=recovered;state.corrupt=final==Read::Corrupt;state.writable=true;
    return recovered?Load::RecoveredTemporary:Load::Loaded;
  }
  state.io=final==Read::IOError||temp==Read::IOError;state.corrupt=final==Read::Corrupt||temp==Read::Corrupt;
  state.writable=!state.io;
  return state.io?Load::IOError:state.corrupt?Load::Corrupt:Load::Missing;
}
Flush workerFlush(uint32_t uptime){
  Event events[Capacity]{};uint8_t count=0;uint32_t newest=0;
  {EVENT_LOCK;if(!state.loaded||!state.dirty)return Flush::Unchanged;
   // An initial I/O failure leaves the old snapshot's validity unknown. Keep
   // new facts in RAM; do not overwrite that potentially valid history.
   if(!state.writable){state.io=true;return Flush::IOError;}
   // A storage failure must not rewrite the same dirty snapshot each idle loop.
   // New facts can be saved immediately; otherwise retry at most every 30 sec.
   if(state.io&&state.attempted==state.newest&&uint32_t(uptime-state.attempted_at)<30000u)return Flush::IOError;
   state.attempted=state.newest;state.attempted_at=uptime;count=state.count;newest=state.newest;
   for(unsigned i=0;i<count;++i)events[i]=state.ring[(state.head+Capacity-state.count+i)%Capacity];}
  uint8_t wire[MaximumSnapshotBytes+1]{};const size_t n=encode(events,count,newest,wire);
  FILE* file=CSP_EVENT_OPEN(Temp,"wb");bool ok=file!=nullptr;
  if(file){
    std::setvbuf(file,nullptr,_IONBF,0);
    if(CSP_EVENT_WRITE(wire,1,n,file)!=n)ok=false;
    if(CSP_EVENT_FLUSH(file)!=0)ok=false;
    if(CSP_EVENT_SYNC(fileno(file))!=0)ok=false;
    if(CSP_EVENT_CLOSE(file)!=0)ok=false;
  }
  uint8_t check[MaximumSnapshotBytes+1]{};size_t checked=0;
  if(ok)ok=readBytes(Temp,check,checked)==Read::Valid&&checked==n&&std::memcmp(check,wire,n)==0;
  if(ok)ok=CSP_EVENT_RENAME(Temp,Final)==0;
#ifndef ARDUINO
  if(ok){const int fd=::open(CSP_EVENT_LOG_ROOT,O_RDONLY|O_DIRECTORY);if(fd<0)ok=false;
    else{if(CSP_EVENT_SYNC(fd)!=0)ok=false;if(::close(fd)!=0)ok=false;}}
#endif
  if(ok)ok=readBytes(Final,check,checked)==Read::Valid&&checked==n&&std::memcmp(check,wire,n)==0;
  EVENT_LOCK;state.io=!ok;
  if(!ok)return Flush::IOError;
  state.persisted=newest;state.dirty=state.newest!=newest;return Flush::Saved;
}
#ifdef CSP_EVENT_LOG_TESTING
void resetForTest(){EVENT_LOCK;state=State{};}
size_t staticBytesForTest(){return sizeof state+sizeof mux;}
#endif
} // namespace pro::events
#endif
