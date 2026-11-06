// Real adapter + exact pinned body/Stream/client loops. TCP/TLS bytes, clock,
// radio, local fixture and receipt storage are synthetic; no socket is opened.
#include "../../src/webdav_client.cpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <sstream>
#include <vector>
#include "actual_http_body.inc"
#include "actual_http_write.inc"
// This signed deadline comparison is present verbatim in the pinned SDK.
// Keep strict runtime sanitizers; suppress only this known compiler warning.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#include "actual_client_read.inc"
#pragma clang diagnostic pop
#define Stream NetworkClient
#include "actual_stream_timed.inc"
#include "actual_stream_line.inc"
#undef Stream
uint32_t fakeNow=100;
unsigned liveTLS=0,liveHTTP=0;
BodyNetwork bodyNetwork;
std::deque<BodyReply> bodyReplies;
std::vector<std::string> bodyMethods;
std::function<void()> bodyPoll;
static bool paused=false,media=true,probeCancel=false;
static unsigned cases=0;
static const std::string Data="Synthetic QA content for exact body-loop tests.";
bool maintenance_active(){return paused;}bool storage_ready(){return media;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){}void backup_progress(uint64_t,uint64_t){}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
 assert(std::string(path)=="/sdcard/QA/f.pdf"&&std::string(mode)=="rb");FILE* f=std::tmpfile();assert(f);
 assert(std::fwrite(Data.data(),1,Data.size(),f)==Data.size());std::rewind(f);return f;
}
static CloudConfig config(){CloudConfig c;c.is_valid=true;c.server_url="https://synthetic.invalid/QA/";c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;}
static void reset(uint32_t now=100){assert(!liveTLS&&!liveHTTP);fakeNow=now;paused=false;media=true;probeCancel=false;bodyReplies.clear();bodyMethods.clear();bodyPoll={};bodyNetwork={};}
static void pass(const char* text){assert(!liveTLS&&!liveHTTP);++cases;std::cout<<"PASS "<<text<<'\n';}
static void rejected(const pro::Result& r,int code){assert(!r.ok&&r.code==code);assert(bodyReplies.empty());}
static BodyReply idle(const char* method="GET",int status=200,int declared=64,ReadMode mode=ReadMode::NegativeIdle){return {method,status,"",declared,false,true,mode};}
int main(){
 auto c=config();WebDAVClient remote(c);
 for(auto mode:{ReadMode::NegativeIdle,ReadMode::ZeroIdle,ReadMode::AvailableZero}){
  reset();bodyReplies={idle("GET",200,64,mode)};const uint32_t before=fakeNow;
  rejected(remote.verify("QA/f.pdf",hash_text(Data),64),-2012);assert(uint32_t(fakeNow-before)>=15000&&uint32_t(fakeNow-before)<15010);
  assert(bodyNetwork.reads>0&&bodyMethods==std::vector<std::string>{"GET"});
  pass("known-length no-data/zero-read peer exits within the existing 15s inactivity budget");
 }
 for(const int length:{64,-1}){
  reset();bodyReplies={idle("GET",200,length)};bodyPoll=[]{if(fakeNow>=300)paused=true;};
  // Unknown-length loops never call read/write when available()==0. Their
  // existing delay(1) advances this simulated clock; TLS polling must stop it.
  rejected(remote.verify("QA/f.pdf",hash_text(Data),64),pro::SnapshotChanged);
  assert(fakeNow<350&&bodyMethods==std::vector<std::string>{"GET"});
  pass("maintenance cancels connected idle known/unknown body without chaining another request");
 }
 reset();bodyReplies={idle("GET",200,-1)};rejected(remote.verify("QA/f.pdf",hash_text(Data),64),-2012);
 assert(fakeNow>=15100&&fakeNow<15110&&bodyNetwork.reads==0);pass("unknown-length available-zero loop terminates without any sink callback");
 reset();bodyReplies={idle()};bodyPoll=[]{if(fakeNow>=300)media=false;};
 rejected(remote.verify("QA/f.pdf",hash_text(Data),64),pro::SnapshotChanged);assert(!paused&&fakeNow<350);
 pass("explicit guarded-source cancellation remains failclosed independently of maintenance");
 reset(UINT32_MAX-8000);bodyReplies={idle()};const auto wrap=fakeNow;
 rejected(remote.verify("QA/f.pdf",hash_text(Data),64),-2012);assert(uint32_t(fakeNow-wrap)>=15000&&uint32_t(fakeNow-wrap)<15010);
 pass("response inactivity deadline remains finite across millis rollover");
 reset();auto partial=idle();partial.bytes="part";bodyReplies={partial};
 rejected(remote.verify("QA/f.pdf",hash_text(Data),64),-2012);assert(bodyNetwork.offset==4);
 pass("positive partial body then stalled peer never verifies a truncated file");
 reset();bodyReplies={{"GET",200,"3\r\nabc\r\n",64,true,true}};
 bodyPoll=[]{if(fakeNow>=200)paused=true;};rejected(remote.verify("QA/f.pdf",hash_text(Data),64),pro::SnapshotChanged);
 assert(fakeNow<15300);pass("stalled chunk-size line exits with cancellation while yielding to the watchdog");
 reset();std::ostringstream chunkSize;chunkSize<<std::hex<<Data.size();const std::string chunk=chunkSize.str()+"\r\n"+Data+"\r\n0\r\n\r\n";
 bodyReplies={{"GET",404,""},{"GET",200,chunk,int(Data.size()),true,true}};
 auto valid=remote.verify("QA/f.pdf",hash_text(Data),Data.size());assert(valid.ok&&valid.code==200);
 pass("actual HTTPClient chunk framing still validates a complete readback");
 reset();bodyReplies={{"GET",200,Data},{"PROPFIND",207,"",64,false,true,ReadMode::NegativeIdle}};
 rejected(remote.verify("QA/f.pdf",hash_text(Data),Data.size()),-2012);
 assert((bodyMethods==std::vector<std::string>{"GET","PROPFIND"}));pass("stalled destination-property body is bounded and cannot publish MOVE");
 reset();bodyReplies={{"GET",200,Data},{"PROPFIND",207,"",-1,false,true}};
 bodyPoll=[]{if(fakeNow>=300)paused=true;};rejected(remote.verify("QA/f.pdf",hash_text(Data),Data.size()),pro::SnapshotChanged);
 pass("unguarded destination-property available-zero body cancels independently of source hash guard");
 reset();bodyReplies={{"GET",200,Data},{"PROPFIND",207,std::string(4097,'x'),-1}};
 auto overflow=remote.verify("QA/f.pdf",hash_text(Data),Data.size());assert(!overflow.ok&&overflow.code==-2011);
 pass("oversized property sink sets persistent write error through actual retry loop");
 reset();{
  ResponseBodyGuard budget(nullptr,nullptr,millis(),1800000);budget.arm();HashSink sink(1,budget);const uint8_t b=0;
  fakeNow+=15000;assert(sink.write(&b,0)==0&&sink.getWriteError()!=0);sink.clearWriteError();
  assert(sink.write(&b,0)==0&&sink.getWriteError()!=0&&!sink.matches(hash_text("")));
 }pass("zero-byte deadline rejection sets terminal Print error even after SDK clears it");
 reset();{
  ResponseBodyGuard budget(nullptr,nullptr,millis(),1000);fakeNow+=1001;budget.arm();
  assert(!budget.ready()&&budget.error()==-2012);
 }pass("arming shared guard never restarts caller total-attempt deadline");
 reset();{
  bool cancel=true;ResponseBodyGuard budget(+[](void* p){return *static_cast<bool*>(p);},&cancel,millis(),30000);budget.arm();
  assert(!budget.ready()&&budget.error()==pro::SnapshotChanged);cancel=false;
  assert(!budget.ready()&&budget.error()==pro::SnapshotChanged);
 }pass("shared cancellation latch remains terminal after callback condition clears");
 reset();paused=true;bodyReplies={idle("PROPFIND",207,-1)};
 auto cancelled=+[](void*){return probeCancel;};bodyPoll=[]{if(fakeNow>=350)probeCancel=true;};
 rejected(remote.probeConnectionReadOnly(cancelled,nullptr),pro::SnapshotChanged);assert(fakeNow<400);
 pass("public read-only probe observes its own cancellation while backup maintenance remains enabled");
 reset();paused=true;bodyReplies={idle("PROPFIND",207,-1)};rejected(remote.probeConnectionReadOnly(cancelled,nullptr),-2012);
 pass("public read-only unknown-length stalled body observes finite inactivity deadline");
 reset();{
  struct Local:pro::Local{bool fingerprint(const std::string&,std::string& h,uint64_t& n)override{h=hash_text(Data);n=Data.size();return true;}std::string key(const std::string&)override{return std::string(64,'a');}} local;
  struct Journal:pro::Journal{pro::Record receipt;bool exists=false;unsigned verifiedWrites=0;
   pro::Load load(const std::string&,pro::Record& out)override{if(!exists)return pro::Load::Missing;out=receipt;return pro::Load::Found;}
   bool save(const std::string&,const pro::Record& in)override{uint8_t bytes[pro::RecordSize];assert(pro::encodeRecord(in,bytes));assert(pro::decodeRecord(bytes,receipt));exists=true;if(in.state==pro::State::Verified)++verifiedWrites;return true;}
  } journal;
  bodyReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""},idle("GET",200,int(Data.size()))};
  pro::Backup engine(local,journal,remote,std::string(64,'b'));const auto result=engine.process("QA/f.pdf",1793793600);
  assert(result.outcome==pro::Outcome::Failed&&result.error==-2012&&journal.verifiedWrites==0&&journal.receipt.state==pro::State::Transferred&&journal.receipt.error==-2012);
 }pass("actual Backup and receipt codec keep transferred state; stalled readback writes no Verified receipt");
 std::cout<<cases<<" actual adapter/pinned body-loop case groups passed\n";
}
