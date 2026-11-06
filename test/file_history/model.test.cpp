#include "file_history.h"
#include "pro_journal.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <atomic>
#include <thread>
namespace fs=std::filesystem;
static bool capturing=false,failAllocation=false,failRead=false,cancelAllocation=false;
static unsigned writeOpens=0,readOpens=0,cases=0;
static size_t allocations=0,frees=0;
static std::map<void*,size_t> live;
static fs::path root;
void* history_test_alloc(size_t n){
  assert(capturing&&n==2240);if(failAllocation)return nullptr;
  void* p=std::malloc(n);assert(p);live[p]=n;++allocations;
  if(cancelAllocation)pro::history::release();return p;
}
void history_test_free(void* p){
  assert(live.count(p)&&live[p]==2240);
  const auto* bytes=static_cast<const uint8_t*>(p);for(size_t i=0;i<live[p];++i)assert(bytes[i]==0);
  live.erase(p);++frees;std::free(p);
}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
  const bool writes=std::strchr(mode,'w')||std::strchr(mode,'a')||std::strchr(mode,'+');
  if(writes){assert(!capturing);++writeOpens;}else ++readOpens;
  if(capturing&&failRead){errno=EIO;return nullptr;}return __real_fopen(path,mode);
}
static std::string identity(64,'a');
static std::string key(unsigned n){char tail[9];std::snprintf(tail,sizeof tail,"%08x",n);return std::string(56,'0')+tail;}
static pro::Record record(std::string path,pro::State state=pro::State::Verified,uint64_t when=1793793600){
  pro::Record r;r.path=std::move(path);r.digest=std::string(64,'d');r.destination=identity;r.state=state;r.sequence=1;r.size=33554432;r.verifiedAt=state==pro::State::Verified?when:0;return r;
}
static std::map<std::string,std::vector<uint8_t>> image(){
  std::map<std::string,std::vector<uint8_t>> result;
  for(const auto& entry:fs::directory_iterator(root))if(entry.is_regular_file()){
    std::ifstream in(entry.path(),std::ios::binary);result[entry.path().filename()]={std::istreambuf_iterator<char>(in),{}};
  }return result;
}
static bool capture(pro::FileJournal& journal){
  const auto before=image();const auto writes=writeOpens;capturing=true;
  const bool result=pro::history::capture(journal,identity,"QA",1793794600);capturing=false;
  assert(image()==before&&writeOpens==writes);return result;
}
static std::string path(uint32_t row){
  const auto s=pro::history::status();std::string result;uint32_t offset=0;
  for(unsigned i=0;i<4;++i){
    pro::history::Fragment f;assert(pro::history::read(s.revision,row,offset,f)==pro::history::Read::Found);
    assert(f.index==row&&f.count==s.count&&f.offset==offset&&f.next<=f.path_bytes&&f.next-offset<=64);
    const std::string h=f.hex;assert(h.size()==(f.next-offset)*2);
    for(size_t j=0;j<h.size();j+=2)result+=char(std::stoul(h.substr(j,2),nullptr,16));
    offset=f.next;if(!f.more){assert(offset==f.path_bytes&&result.size()==f.path_bytes);return result;}
  }assert(false);return {};
}
static void pass(const char* name){++cases;std::cout<<"PASS "<<name<<'\n';}
int main(int argc,char** argv){
  assert(argc==2);root=argv[1];fs::remove_all(root);fs::create_directories(root);pro::FileJournal journal(root.string());
  assert(pro::history::residentBytesForTest()<=64&&pro::history::cacheBytesForTest()==0);
  assert(capture(journal));auto s=pro::history::status();assert(s.loaded&&s.complete&&!s.count&&live.empty());
  pass("empty receipt directory is an explicit read-only empty snapshot with no cache allocation");
  for(unsigned i=0;i<10;++i)assert(journal.save(key(i),record("QA/synthetic-"+std::to_string(i)+".pdf",pro::State::Verified,1793793600+i)));
  const std::string quotes="QA/"+std::string(237,'\"');
  auto waiting=record(quotes,pro::State::Pending);waiting.error=-3;waiting.attempts=2;waiting.next=1793799999;waiting.size=UINT64_MAX;
  assert(journal.save(key(10),waiting));
  std::string unicode="QA/"+std::string(60,'x');for(unsigned i=0;i<44;++i)unicode+="\xf0\x9f\x93\x84";unicode+='z';assert(unicode.size()==240&&pro::validPath(unicode));
  assert(journal.save(key(11),record(unicode,pro::State::Transferred)));
  auto excluded=record("QA/foreign.pdf");excluded.destination=std::string(64,'b');assert(journal.save(key(12),excluded));
  assert(journal.save(key(13),record("QA-other/not-watched.pdf")));
  assert(capture(journal));s=pro::history::status();assert(s.count==8&&s.matched==12&&s.limited&&s.complete);
  assert(pro::history::cacheBytesForTest()==2240&&live.size()==1);
  assert(path(0)==quotes&&path(1)==unicode);
  std::set<std::string> visible;for(unsigned i=0;i<s.count;++i)visible.insert(path(i));
  assert(visible.count("QA/synthetic-9.pdf")&&!visible.count("QA/synthetic-0.pdf")&&!visible.count(excluded.path));
  pro::history::Fragment f;assert(pro::history::read(s.revision,0,0,f)==pro::history::Read::Found);
  assert(f.bytes==UINT64_MAX&&f.error==-3&&f.attempts==2&&f.state==0&&f.verified==0);
  pass("eight bounded waiting/latest-verified rows preserve numeric state and exact quote/UTF-8 path bytes");
  assert(pro::history::read(s.revision+1,0,0,f)==pro::history::Read::Stale);
  assert(pro::history::read(s.revision,8,0,f)==pro::history::Read::Invalid);
  assert(pro::history::read(s.revision,0,241,f)==pro::history::Read::Invalid);
  assert(pro::history::read(s.revision,0,240,f)==pro::history::Read::Found&&!f.more&&f.next==240&&!f.hex[0]);
  const auto old=s.revision;pro::history::release();assert(pro::history::read(old,0,0,f)==pro::history::Read::Stale&&live.empty());
  pass("revision/index/offset boundaries reject and release erases the entire cache before free");
  failAllocation=true;assert(!capture(journal));failAllocation=false;s=pro::history::status();
  assert(s.loaded&&s.allocation_failed&&s.count==0&&pro::history::cacheBytesForTest()==0&&live.empty());
  assert(pro::history::read(s.revision,0,0,f)==pro::history::Read::Unavailable);
  pass("allocation failure is explicit, leaves zero cache heap and never edits receipts");
  cancelAllocation=true;assert(!capture(journal));cancelAllocation=false;
  s=pro::history::status();assert(!s.loaded&&s.count==0&&live.empty());
  pass("retirement generation revoked during capture cannot publish and wipes unpublished private rows");
  assert(capture(journal));const auto concurrent=pro::history::status().revision;
  std::atomic<bool> entered{false},done{false};
  std::thread reader([&]{entered=true;while(!done){pro::history::Fragment row;const auto state=pro::history::read(concurrent,0,0,row);assert(state==pro::history::Read::Found||state==pro::history::Read::Stale);}});
  while(!entered.load())std::this_thread::yield();pro::history::release();done=true;reader.join();assert(live.empty());
  pass("coherent fragment read racing owner release never dereferences detached/erased cache");
  failRead=true;assert(!capture(journal));failRead=false;s=pro::history::status();assert(s.io_failed&&!s.complete&&s.count==0&&live.empty());
  pass("receipt I/O failure is explicit rather than a false complete empty result");
  fs::remove_all(root);fs::create_directories(root);
  auto r=record("QA/dual.pdf",pro::State::Pending);assert(journal.save(key(20),r));
  r.state=pro::State::Transferred;r.sequence=2;assert(journal.save(key(20),r));
  r.state=pro::State::Verified;r.sequence=3;r.verifiedAt=1793793600;assert(journal.save(key(20),r));
  const auto bad=root/(pro::journalStem(key(20))+".1");{std::ofstream out(bad,std::ios::binary|std::ios::trunc);out<<"damaged synthetic newest receipt";}
  assert(capture(journal));s=pro::history::status();assert(s.count==1&&path(0)=="QA/dual.pdf");
  assert(pro::history::read(s.revision,0,0,f)==pro::history::Read::Found&&f.state==1&&f.verified==0);
  pass("actual dual-generation journal fallback never promotes a damaged newest Verified record");
  fs::remove_all(root);fs::create_directories(root);
  {std::ofstream out(root/(pro::journalStem(key(21))+".0"));out<<"both generations unavailable";}
  assert(capture(journal));s=pro::history::status();assert(s.corrupt&&s.complete&&s.count==0&&live.empty());
  pass("unreadable CRC record is skipped with a corruption flag, not fabricated file success");
  fs::remove_all(root);fs::create_directories(root);
  for(unsigned i=0;i<pro::FileLimit;++i)assert(journal.save(key(i),record("QA/full-"+std::to_string(i)+".pdf",pro::State::Verified,1793793600+i)));
  assert(capture(journal));s=pro::history::status();assert(s.matched==128&&s.count==8&&s.limited&&!s.complete&&pro::history::cacheBytesForTest()==2240);
  pass("128-receipt directory remains an eight-row allocation and truthfully marks enumeration bound");
  pro::history::release();assert(pro::history::seedRevisionForTest(UINT32_MAX-1));
  assert(capture(journal));s=pro::history::status();assert(s.revision==UINT32_MAX&&s.loaded&&!s.exhausted&&s.count==8);
  assert(pro::history::read(UINT32_MAX,0,0,f)==pro::history::Read::Found);
  pro::history::release();s=pro::history::status();assert(s.revision==UINT32_MAX&&s.exhausted&&!s.loaded&&live.empty());
  assert(!capture(journal));assert(pro::history::status().exhausted&&live.empty());
  pass("last valid revision is readable, then release permanently latches exhaustion without wrap");
  assert(pro::history::seedRevisionForTest(UINT32_MAX-1));
  cancelAllocation=true;assert(!capture(journal));cancelAllocation=false;s=pro::history::status();
  assert(s.revision==UINT32_MAX&&s.exhausted&&!s.loaded&&s.count==0&&live.empty());
  assert(pro::history::read(UINT32_MAX,0,0,f)==pro::history::Read::Unavailable);
  assert(!capture(journal)&&pro::history::status().exhausted&&live.empty());
  pass("same-revision concurrent release at UINT32_MAX cannot overwrite the terminal exhaustion latch");
  pro::history::release();assert(live.empty()&&allocations==frees);
  std::cout<<cases<<" actual journal/history cases passed; resident="<<pro::history::residentBytesForTest()<<" cache_max=2240\n";
}
