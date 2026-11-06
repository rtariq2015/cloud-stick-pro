// Actual health publisher/DAV + exact frozen HTTP/Stream/client body loops.
// TCP/TLS bytes and clock are controlled; no real socket or filesystem access.
#include "../../src/webdav_client.cpp"
#include "../../src/remote_health_publisher.cpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <sstream>
#include <vector>
#include "actual_http_body.inc"
#include "actual_http_write.inc"
// Verbatim pinned NetworkClient has one signed deadline comparison. Do not
// suppress any sanitizer diagnostic or change the extracted SDK function.
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
std::function<void(unsigned)> bodyRequestPoll;
static bool paused=false;
static unsigned cases=0;
bool maintenance_active(){return paused;}
bool storage_ready(){assert(false&&"health must not acquire/read SD");return false;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){assert(false);}
void backup_progress(uint64_t,uint64_t){assert(false);}
extern "C" FILE* __wrap_fopen(const char*,const char*){assert(false&&"health opened a file");return nullptr;}
using namespace pro::health;

static CloudConfig configuration(){CloudConfig c;c.is_valid=true;c.server_url="https://synthetic.invalid/QA/";
  c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;}
static Snapshot observation(){Snapshot s;std::strcpy(s.firmware,"0.4.0-product-lab4");s.boot_id=1;s.sequence=1;
  s.utc_valid=true;s.reported_at=1793828957;s.uptime_ms=100;s.wifi=Wifi::Connected;s.wifi_observed_at=s.reported_at;
  s.sd=Sd::Initialized;s.usb=UsbOwnership::Host;return s;}
static std::string scope(){const auto c=configuration();return hash_text("CSP-health-v1\n"+
  std::to_string(ESP.getEfuseMac())+"\n"+c.server_url.c_str()+"\n"+c.username.c_str());}
static std::string ownerMarker(){return "CSP-HEALTH-OWNER/1\n"+scope()+"\n";}
static std::string envelope(){char body[ReportCapacity];size_t n=0;assert(encode(observation(),body,sizeof body,n));
  return "CSP-HEALTH/1\nowner="+scope()+"\nrevision=1\nlength="+std::to_string(n)+
    "\nsha256="+hash_text(std::string(body,n))+"\n\n"+std::string(body,n);}
static BodyReply idle(int length,ReadMode mode=ReadMode::NegativeIdle){return {"GET",200,"",length,false,true,mode};}
static std::string properties(){return "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:propstat><d:prop>"
  "<d:resourcetype/><d:getetag>\"owned-slot\"</d:getetag></d:prop><d:status>HTTP/1.1 200 OK</d:status>"
  "</d:propstat></d:response></d:multistatus>";}
enum class Stage {Marker,Inspect,Final};
static void reset(uint32_t now=100){assert(!liveTLS&&!liveHTTP);fakeNow=now;paused=false;
  bodyReplies.clear();bodyMethods.clear();bodyPoll={};bodyRequestPoll={};bodyNetwork={};}
static unsigned responses(Stage stage,BodyReply last){
  if(stage==Stage::Marker)bodyReplies={std::move(last)};
  else if(stage==Stage::Inspect)bodyReplies={{"GET",200,ownerMarker()},
    {"PROPFIND",207,properties()},std::move(last)};
  else bodyReplies={{"GET",200,ownerMarker()},{"PROPFIND",404,""},
    {"PROPFIND",404,""},{"PUT",201,""},std::move(last)};
  return unsigned(bodyReplies.size());
}
static Publication publish(){const auto c=configuration();WebDAVClient remote(c);
  return remote.publishHealth(observation(),{true,true,true});}
static void failed(const Publication& r,int error,unsigned sent){assert(!r.ok&&r.code==error);
  assert(bodyReplies.empty()&&bodyMethods.size()==sent&&!liveTLS&&!liveHTTP);}
static void pass(const char* text){assert(!liveTLS&&!liveHTTP);++cases;std::cout<<"PASS "<<text<<'\n';}

int main(){
  for(auto stage:{Stage::Marker,Stage::Inspect,Stage::Final}) {
    for(auto mode:{ReadMode::NegativeIdle,ReadMode::ZeroIdle,ReadMode::AvailableZero}) {
      reset();const int size=stage==Stage::Marker?int(ownerMarker().size()):512;
      const auto sent=responses(stage,idle(size,mode));const auto start=fakeNow;
      failed(publish(),HealthDeadline,sent);
      assert(uint32_t(fakeNow-start)>=15000&&uint32_t(fakeNow-start)<15010&&bodyNetwork.reads>0);
      pass("each health GET known-length no-data/zero-read terminates with health deadline");
    }
    for(int length:{0,-1}) {
      reset();const int size=length==0?(stage==Stage::Marker?int(ownerMarker().size()):512):-1;
      const auto sent=responses(stage,idle(size));
      bodyPoll=[]{if(fakeNow>=300)paused=true;};
      failed(publish(),pro::SnapshotChanged,sent);assert(fakeNow<350);
      pass("each health GET known/unknown no-data body cancels without another request");
    }
    reset();const auto sent=responses(stage,idle(-1));const auto start=fakeNow;
    failed(publish(),HealthDeadline,sent);
    assert(uint32_t(fakeNow-start)>=15000&&uint32_t(fakeNow-start)<15010&&bodyNetwork.reads==0);
    pass("each health GET unknown-length available-zero exits without sink callback");
    reset();const auto sent2=responses(stage,idle(-1));
    bodyRequestPoll=[sent2](unsigned n){if(n==sent2)fakeNow+=29900;};
    failed(publish(),HealthDeadline,sent2);assert(fakeNow>=30100&&fakeNow<30110);
    pass("each health GET preserves original 30s total start when arming body guard");
    reset();const auto complete=stage==Stage::Marker?ownerMarker():envelope();
    const auto partial=stage==Stage::Marker?complete.substr(0,7):complete.substr(0,complete.find("\n\n")+2+12);
    const auto sent3=responses(stage,{"GET",200,partial,stage==Stage::Marker?int(ownerMarker().size()):-1,false,true});
    failed(publish(),HealthDeadline,sent3);assert(bodyNetwork.offset==partial.size());
    pass("positive owned partial health body then stall neither repairs nor acknowledges a report");
  }
  reset(UINT32_MAX-8000);const auto start=fakeNow;
  responses(Stage::Marker,idle(int(ownerMarker().size())));failed(publish(),HealthDeadline,1);
  assert(uint32_t(fakeNow-start)>=15000&&uint32_t(fakeNow-start)<15010);
  pass("health inactivity deadline remains finite across millis rollover");
  reset();responses(Stage::Final,{"GET",200,envelope()});const auto good=publish();
  assert(good.ok&&good.revision==1&&good.slot==0&&bodyReplies.empty());
  pass("complete health publication still validates exact envelope through pinned loop");
  reset();std::ostringstream size;size<<std::hex<<envelope().size();
  responses(Stage::Final,{"GET",200,size.str()+"\r\n"+envelope()+"\r\n0\r\n\r\n",int(envelope().size()),true,true});
  const auto chunked=publish();assert(chunked.ok&&chunked.revision==1&&bodyReplies.empty());
  pass("pinned chunked framing still verifies a complete health envelope");
  reset();responses(Stage::Final,{"GET",200,"8\r\npartial!\r\n",512,true,true});
  bodyPoll=[]{if(fakeNow>=300)paused=true;};failed(publish(),pro::SnapshotChanged,5);
  assert(fakeNow<15320);pass("health cancellation terminates stalled chunk-size line without false acknowledgment");
  reset();{
    const auto cancel=+[](void*){return paused;};
    pro::http_response::Guard guard(cancel,nullptr,fakeNow,30000);guard.arm();
    ExactSink marker("x",1,guard);const uint8_t x='x';paused=true;
    assert(marker.write(&x,0)==0&&marker.getWriteError());marker.clearWriteError();paused=false;
    assert(marker.write(&x,0)==0&&marker.getWriteError()&&!marker.matches());
  }pass("marker sink retains terminal cancellation through SDK zero-input retry");
  reset();{
    const auto cancel=+[](void*){return paused;};
    pro::http_response::Guard guard(cancel,nullptr,fakeNow,30000);guard.arm();
    const auto own=scope();EnvelopeSink sink(own.c_str(),guard);const uint8_t zero=0;fakeNow+=15000;
    assert(sink.write(&zero,0)==0&&sink.getWriteError());sink.clearWriteError();
    assert(sink.write(&zero,0)==0&&sink.getWriteError()&&!sink.valid()&&!sink.truncated());
  }pass("envelope sink retains terminal inactivity failure through SDK zero-input retry");
  reset();{
    pro::http_response::Guard guard(nullptr,nullptr,fakeNow,30000);guard.arm();
    ExactSink marker("x",1,guard);const uint8_t wrong='y',right='x';
    assert(marker.write(&wrong,1)==0&&marker.getWriteError());marker.clearWriteError();
    assert(marker.write(&right,0)==0&&marker.getWriteError()&&!marker.matches()&&guard.ready());
  }pass("marker mismatch remains terminal across zero-input retry with a healthy guard");
  reset();{
    pro::http_response::Guard guard(nullptr,nullptr,fakeNow,30000);guard.arm();
    const auto own=scope();EnvelopeSink sink(own.c_str(),guard);const uint8_t bad=0;
    assert(sink.write(&bad,1)==0&&sink.getWriteError());sink.clearWriteError();
    assert(sink.write(&bad,0)==0&&sink.getWriteError()&&!sink.valid()&&!sink.truncated()&&guard.ready());
  }pass("malformed envelope remains terminal across zero-input retry with a healthy guard");
  std::cout<<cases<<" actual health/pinned-loop case groups passed\n";
}
