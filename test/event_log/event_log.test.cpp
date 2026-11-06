#include "pro_event_log.h"
#include "io.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <atomic>
#include <vector>
namespace fs=std::filesystem;
using namespace pro::events;
const fs::path root=CSP_EVENT_LOG_ROOT;
static unsigned cases=0;
static void pass(const char* name){++cases;std::cout<<"PASS "<<name<<'\n';}
static std::vector<uint8_t> contents(const char* name="events.log"){
 std::ifstream file(root/name,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
static void write(const std::vector<uint8_t>& bytes,const char* name="events.log"){
 std::ofstream file(root/name,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(file.good());
}
static uint32_t crc(const uint8_t* p,size_t n,uint32_t value=0xffffffffu){for(size_t i=0;i<n;++i){value^=p[i];for(unsigned j=0;j<8;++j)value=(value>>1)^(0xedb88320u&uint32_t(0-int32_t(value&1)));}return value;}
static void put32(uint8_t* p,uint32_t value){for(unsigned i=0;i<4;++i)p[i]=uint8_t(value>>(8*i));}
static void checksum(std::vector<uint8_t>& bytes){put32(bytes.data()+16,~crc(bytes.data()+20,bytes.size()-20,crc(bytes.data(),16)));}
static void reset(){event_io::clear();resetForTest();fs::remove_all(root);fs::create_directories(root/"queue");write({1,2,3,4},"queue/receipt.fixture");}
static void originalRetained(){assert(contents("queue/receipt.fixture")==std::vector<uint8_t>({1,2,3,4}));assert(event_io::paths.empty());}
static void add(unsigned n=1){for(unsigned i=0;i<n;++i)assert(record(Kind::WorkerStart,int32_t(i),uint64_t(i)*1024,1793793600,123+i));}
static std::vector<Event> pages(){std::vector<Event> events;uint32_t cursor=0;for(unsigned page=0;page<4;++page){const auto p=readPage(cursor);events.insert(events.end(),p.events,p.events+p.count);if(!p.more)break;assert(p.next>cursor);cursor=p.next;}return events;}
int main(){
 reset();assert(staticBytesForTest()<=512);assert(MaximumSnapshotBytes==404);assert(workerLoad()==Load::Missing);assert(workerFlush()==Flush::Unchanged);assert(event_io::writes==0);pass("static RAM <=512, missing history does not create a file");
 add();assert(workerFlush()==Flush::Saved);auto baseline=contents();assert(baseline.size()==52);assert(baseline[0]=='C'&&baseline[6]==1&&baseline[7]==0);
 // Independent Python struct.pack('<IIQQiB3x') + zlib.crc32 golden vector.
 const std::string golden="435350455654010001000000010020007f663fe8010000007b000000401eeb6a0000000000000000000000000000000002000000";
 for(unsigned i=0;i<baseline.size();++i)assert(baseline[i]==std::stoul(golden.substr(i*2,2),nullptr,16));
 assert(baseline[20]==1&&baseline[24]==123&&baseline[48]==uint8_t(Kind::WorkerStart));assert(baseline[49]==0&&baseline[50]==0&&baseline[51]==0);
 resetForTest();assert(workerLoad()==Load::Loaded);auto history=pages();assert(history.size()==1&&history[0].utc_unix==1793793600&&history[0].code==0);originalRetained();pass("explicit little-endian wire format and persisted roundtrip");
 reset();assert(workerLoad()==Load::Missing);add(25);auto events=pages();assert(events.size()==Capacity&&events.front().sequence==14&&events.back().sequence==25);
 assert(readPage(25).count==0&&!readPage(25).more);assert(workerFlush()==Flush::Saved);assert(contents().size()==MaximumSnapshotBytes);resetForTest();assert(workerLoad()==Load::Loaded);assert(pages().front().sequence==14);pass("ring, storage and read-only pagination remain bounded");
 reset();workerLoad();assert(record(Kind::Error,-2303,0,0,1));assert(record(Kind::ScanOutcome,-2010,0,0,2));assert(record(Kind::VerifiedCount,0,0,0,3));assert(workerFlush()==Flush::Saved);const auto written=event_io::writes;
 for(unsigned i=0;i<100;++i){assert(!record(Kind::Error,-2303,0,0,10+i));assert(!record(Kind::ScanOutcome,-2010,0,0,10+i));assert(!record(Kind::VerifiedCount,0,0,0,10+i));assert(workerFlush()==Flush::Unchanged);}
 assert(event_io::writes==written);assert(record(Kind::Error,0,0,1793793600,200));assert(record(Kind::Error,-2303,0,1793793600,201));pass("per-kind coalescing avoids idle flash wear and records recovery/repeated failure");
 reset();workerLoad();assert(record(Kind::Error,-1,UINT64_MAX,1,UINT32_MAX));assert(pages()[0].utc_unix==0&&pages()[0].bytes==UINT64_MAX&&pages()[0].uptime_ms==UINT32_MAX);
 assert(!record(Kind(0),0,0,0,0)&&!record(Kind(255),0,0,0,0)&&!record(Kind::VerifiedCount,-1,0,0,0)&&!record(Kind::VerifiedCount,129,0,0,0));pass("invalid clock is zero; numeric limits and kind/count admission are explicit");
 const char* invalid[]={"crc","magic","reserved","count","record-size","kind","sequence","utc","count-kind","length","trailing","header-reserved"};
 for(const char* invalidCase:invalid){
  reset();auto bad=baseline;const std::string mode=invalidCase;
  if(mode=="crc")bad.back()^=1;
  if(mode=="magic")bad[0]=0;
  if(mode=="reserved")bad[49]=1;
  if(mode=="count")bad[12]=13;
  if(mode=="record-size")bad[14]=31;
  if(mode=="kind")bad[48]=255;
  if(mode=="sequence")put32(bad.data()+20,0);
  if(mode=="utc")for(unsigned i=0;i<8;++i)bad[28+i]=255;
  if(mode=="count-kind"){bad[48]=uint8_t(Kind::VerifiedCount);put32(bad.data()+44,129);}
  if(mode=="length")bad.pop_back();
  if(mode=="trailing")bad.push_back(0);
  if(mode=="header-reserved")bad[13]=1;
  if(mode!="crc"&&mode!="length"&&mode!="trailing")checksum(bad);
  write(bad);assert(workerLoad()==Load::Corrupt);assert(status().corrupt&&status().count==0);assert(contents()==bad);assert(event_io::writes==0);originalRetained();
  pass(("corrupt input: "+mode).c_str());
 }
 for(size_t n=0;n<baseline.size();++n){reset();write({baseline.begin(),baseline.begin()+n});assert(workerLoad()==Load::Corrupt);assert(status().count==0&&event_io::writes==0);originalRetained();}
 pass("every truncated prefix rejects without repair writes or formatting");
 reset();write(std::vector<uint8_t>(4097,0xff));assert(workerLoad()==Load::Corrupt);assert(contents().size()==4097&&event_io::writes==0);pass("oversized input is read with a fixed 405-byte cap and never copied to RAM");
 reset();write(baseline,"events.tmp");assert(workerLoad()==Load::RecoveredTemporary);assert(status().dirty&&status().persisted_sequence==0);assert(workerFlush()==Flush::Saved);assert(contents()==baseline);pass("complete temporary snapshot recovers when final is absent");
 reset();write({1,2,3});write(baseline,"events.tmp");assert(workerLoad()==Load::RecoveredTemporary);assert(status().corrupt);assert(workerFlush()==Flush::Saved);assert(contents()==baseline);pass("valid temporary recovers a corrupt final without touching receipts");
 reset();write(baseline);auto newer=baseline;put32(newer.data()+8,2);put32(newer.data()+20,2);checksum(newer);write(newer,"events.tmp");assert(workerLoad()==Load::Loaded);assert(status().newest_sequence==1);assert(contents()==baseline);pass("published valid final wins over an uncommitted temporary snapshot");
 reset();write(baseline);assert(workerLoad()==Load::Loaded);add();event_io::failure="rename";assert(workerFlush()==Flush::IOError);assert(status().dirty&&status().io_failed);event_io::failure.clear();assert(workerLoad()==Load::AlreadyLoaded);assert(status().count==2);assert(workerFlush(30000)==Flush::Saved);pass("failed flush RAM survives worker restart instead of being replaced by older disk");
 for(const char* mode:{"open-write","write","flush","sync","close","readback","rename"}){
  reset();write(baseline);assert(workerLoad()==Load::Loaded);add();event_io::failure=mode;assert(workerFlush()==Flush::IOError);assert(status().dirty&&status().io_failed);assert(contents()==baseline);originalRetained();event_io::failure.clear();assert(workerFlush(30000)==Flush::Saved);assert(status().persisted_sequence==2&&!status().dirty&&!status().io_failed);pass((std::string("atomic failure preserves old valid snapshot: ")+mode).c_str());
 }
 for(const char* mode:{"open-read","read","close"}){reset();write(baseline);event_io::failure=mode;assert(workerLoad()==Load::IOError);assert(status().io_failed&&status().count==0&&event_io::writes==0);originalRetained();event_io::failure.clear();add();assert(workerFlush()==Flush::IOError&&event_io::writes==0&&contents()==baseline);pass((std::string("read failure preserves unknown old history and remains RAM-only: ")+mode).c_str());}
 reset();write(baseline);assert(workerLoad()==Load::Loaded);add();event_io::failure="rename";assert(workerFlush(0xfffffff0u)==Flush::IOError);const auto attempted=event_io::writes;
 for(unsigned i=0;i<100;++i)assert(workerFlush(5)==Flush::IOError);assert(event_io::writes==attempted);event_io::failure.clear();assert(workerFlush(30000)==Flush::Saved);pass("same failed batch is throttled across uptime wrap without blocking new diagnostics");
 reset();workerLoad();add();bool injected=false;event_io::before=[&](const char* op){if(!injected&&std::string(op)=="write"){injected=true;assert(record(Kind::Error,-401,0,0,5));}};
 assert(workerFlush()==Flush::Saved);assert(status().dirty&&status().persisted_sequence==1&&status().newest_sequence==2);event_io::before={};assert(workerFlush()==Flush::Saved);assert(!status().dirty);pass("events arriving during storage I/O stay dirty and are saved in a later batch");
 reset();auto exhausted=baseline;put32(exhausted.data()+8,UINT32_MAX);put32(exhausted.data()+20,UINT32_MAX);checksum(exhausted);write(exhausted);assert(workerLoad()==Load::Loaded);assert(!record(Kind::Error,-2302,0,0,0));assert(status().sequence_exhausted&&!status().dirty&&status().newest_sequence==UINT32_MAX);assert(workerFlush()==Flush::Unchanged);pass("sequence exhaustion freezes diagnostics without wrap, UB or storage writes");
 reset();workerLoad();std::atomic<bool> done{false};std::thread reader([&]{while(!done){const auto p=readPage();assert(p.count<=PageCapacity&&p.status.count<=Capacity);for(unsigned i=1;i<p.count;++i)assert(p.events[i].sequence>p.events[i-1].sequence);}});
 for(unsigned i=0;i<1000;++i)assert(record(Kind::WorkerStart,0,0,0,i));done=true;reader.join();assert(status().count==Capacity);pass("concurrent RAM pages see coherent bounded snapshots");
 originalRetained();std::cout<<"static_bytes="<<staticBytesForTest()<<" snapshot_max_bytes="<<MaximumSnapshotBytes<<'\n';std::cout<<cases<<" sanitized event-log cases passed\n";
}
