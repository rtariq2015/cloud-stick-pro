#include "file_history.h"
#if CSP_ENABLE_FILE_HISTORY
#include "pro_journal.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <mutex>
#endif
#ifndef CSP_HISTORY_ALLOC
#define CSP_HISTORY_ALLOC std::malloc
#define CSP_HISTORY_FREE std::free
#endif
namespace pro::history {
namespace {
static_assert(PathLimit==240,"History protocol path limit must be reviewed");
struct alignas(8) Row {
  uint64_t bytes=0,verified=0,retry=0;
  uint32_t attempts=0;int32_t error=0;
  char path[PathLimit+1]{};
  uint8_t state=0,length=0;
};
static_assert(sizeof(Row)==280,"History row budget changed");
enum Flag:uint8_t {Loaded=1,Complete=2,Limited=4,Corrupt=8,IOFailed=16,AllocationFailed=32,Exhausted=64};
struct Packed {uint64_t utc=0;uint32_t revision=0;uint16_t matched=0;uint8_t count=0,flags=0;};
struct Control {Row* rows=nullptr;Packed state;};
Control control;
#ifdef ARDUINO
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
struct Lock {Lock(){portENTER_CRITICAL(&mux);}~Lock(){portEXIT_CRITICAL(&mux);}};
#else
std::mutex mux;
struct Lock {std::lock_guard<std::mutex> guard{mux};};
#endif
static_assert(sizeof(control)+sizeof(mux)<=64,"History resident control budget exceeded");
void wipe(Row* rows){
  if(!rows)return;
  volatile uint8_t* p=reinterpret_cast<volatile uint8_t*>(rows);
  for(size_t i=0;i<Capacity*sizeof(Row);++i)p[i]=0;
  CSP_HISTORY_FREE(rows);
}
struct ScopedRows {Row* value=nullptr;~ScopedRows(){wipe(value);}};
bool earlier(const Row& a,const Row& b){
  // Waiting/retry records first; their next retry is a scheduling fact, not an
  // invented last-upload timestamp. Verified rows are newest verification first.
  const bool waitingA=a.state!=uint8_t(State::Verified),waitingB=b.state!=uint8_t(State::Verified);
  if(waitingA!=waitingB)return waitingA;
  const uint64_t timeA=waitingA?a.retry:a.verified,timeB=waitingB?b.retry:b.verified;
  if(timeA!=timeB)return timeA>timeB;
  return std::strcmp(a.path,b.path)<0;
}
}
void release(){
  Row* rows=nullptr;
  {Lock lock;rows=control.rows;control.rows=nullptr;
    const auto old=control.state.revision;control.state=Packed{};
    control.state.revision=old==UINT32_MAX?old:old+1;
    if(old==UINT32_MAX)control.state.flags=Exhausted;
  }
  wipe(rows);
}
bool capture(FileJournal& journal,const std::string& destination,const std::string& folder,uint64_t utc){
  release();uint32_t revision=0;
  {Lock lock;revision=control.state.revision;if(control.state.flags&Exhausted)return false;}
  Packed state;state.revision=revision;state.utc=utc;state.flags=Loaded;
  ScopedRows cleanup;Row*& rows=cleanup.value;
  if(!validHex(destination.c_str())||!validPath(folder)){state.flags|=IOFailed;}
  else{
    const std::string prefix=folder+"/";std::string cursor,next;Record record;
    for(size_t examined=0;examined<FileLimit;++examined){
      const auto result=journal.next(cursor,next,record);
#ifdef ARDUINO
      // Each next() has closed its directory/file handles. Yield while the
      // optional read-only retirement scan still owns metadata, rather than
      // monopolizing the worker core across up to 128 bounded enumerations.
      vTaskDelay(1);
#endif
      if(result==Load::Missing){state.flags|=Complete;break;}
      if(result==Load::IOError){state.flags|=IOFailed;break;}
      if(next.empty()||next<=cursor){state.flags|=IOFailed;break;}cursor=next;
      if(result!=Load::Found){state.flags|=Corrupt;continue;}
      if(record.destination!=destination||record.path.compare(0,prefix.size(),prefix)!=0)continue;
      ++state.matched;
      if(!rows){
        rows=static_cast<Row*>(CSP_HISTORY_ALLOC(Capacity*sizeof(Row)));
        if(!rows){state.flags|=AllocationFailed;break;}
        // Row is trivial, fixed storage; no per-path allocation survives.
        for(size_t i=0;i<Capacity;++i)new(rows+i)Row{};
      }
      Row candidate;candidate.bytes=record.size;candidate.verified=record.verifiedAt;candidate.retry=record.next;
      candidate.attempts=record.attempts;candidate.error=record.error;candidate.state=uint8_t(record.state);
      candidate.length=uint8_t(record.path.size());std::memcpy(candidate.path,record.path.data(),record.path.size());
      size_t at=0;while(at<state.count&&!earlier(candidate,rows[at]))++at;
      if(at<Capacity){
        const size_t end=std::min<size_t>(state.count,Capacity-1);
        for(size_t i=end;i>at;--i)rows[i]=rows[i-1];rows[at]=candidate;
        if(state.count<Capacity)++state.count;
      }
    }
  }
  if(state.matched>state.count||!(state.flags&Complete))state.flags|=Limited;
  bool published=false;
  {Lock lock;if(control.state.revision==revision&&!(control.state.flags&Exhausted)&&!control.rows){control.rows=rows;rows=nullptr;control.state=state;published=true;}}
  return published&&!(state.flags&(IOFailed|AllocationFailed));
}
Status status(){
  Lock lock;const auto& s=control.state;
  return {s.utc,s.revision,s.matched,s.count,bool(s.flags&Loaded),bool(s.flags&Complete),bool(s.flags&Limited),
          bool(s.flags&Corrupt),bool(s.flags&IOFailed),bool(s.flags&AllocationFailed),bool(s.flags&Exhausted)};
}
Read read(uint32_t revision,uint32_t index,uint32_t offset,Fragment& out){
  out=Fragment{};Lock lock;
  const auto& s=control.state;
  if(revision!=s.revision)return Read::Stale;
  if(!(s.flags&Loaded)||s.flags&AllocationFailed||s.flags&Exhausted)return Read::Unavailable;
  if(!control.rows||index>=s.count||offset>control.rows[index].length)return Read::Invalid;
  const auto& row=control.rows[index];
  out.bytes=row.bytes;out.verified=row.verified;out.retry=row.retry;out.attempts=row.attempts;out.error=row.error;
  out.revision=s.revision;out.index=uint8_t(index);out.count=s.count;out.state=row.state;out.path_bytes=row.length;
  out.offset=uint16_t(offset);out.next=uint16_t(std::min<size_t>(row.length,offset+FragmentBytes));out.more=out.next<row.length;
  constexpr char hex[]="0123456789abcdef";
  for(size_t i=offset;i<out.next;++i){const auto byte=uint8_t(row.path[i]);out.hex[(i-offset)*2]=hex[byte>>4];out.hex[(i-offset)*2+1]=hex[byte&15];}
  return Read::Found;
}
#ifdef CSP_FILE_HISTORY_TESTING
size_t residentBytesForTest(){return sizeof(control)+sizeof(mux);}
size_t cacheBytesForTest(){Lock lock;return control.rows?Capacity*sizeof(Row):0;}
bool seedRevisionForTest(uint32_t revision){
  Lock lock;if(control.rows)return false;
  control.state=Packed{};control.state.revision=revision;return true;
}
#endif
}
#endif
