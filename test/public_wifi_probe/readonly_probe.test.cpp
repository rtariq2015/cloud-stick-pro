// Actual WebDAV adapter, synthetic HTTP/TLS/clock/radio only. Every physical
// file/storage/transfer callback is counted and must remain unused by the probe.
#include "webdav_client.h"
#include "pro_device.h"
#include "usb_storage.h"
#include "koofr_ca.h"
#include "probe_state.h"
#include <cassert>
#include <ctime>
#include <iostream>
#include <new>
#include <type_traits>
using probe_test::state;
uint32_t fakeNow=0;
static unsigned passed=0;
bool maintenance_active(){++state.maintenanceReads;return true;}
bool storage_ready(){++state.storageReads;return false;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){++state.transferEvents;}
void backup_progress(uint64_t,uint64_t){++state.transferEvents;}
extern "C" FILE* __wrap_fopen(const char*,const char*){++state.fileOpens;assert(false);return nullptr;}
extern "C" time_t __wrap_time(time_t* out){const auto now=time_t(state.utc);if(out)*out=now;return now;}
void* operator new[](std::size_t size,const std::nothrow_t&)noexcept{
  if(state.failBuffer&&size==4096)return nullptr;
  try{return ::operator new[](size);}catch(...){return nullptr;}
}
static CloudConfig configuration(){
  CloudConfig c;c.is_valid=true;c.server_url="https://dav.example.test/QA/";
  c.username="synthetic-probe-user";c.password="synthetic-probe-password";c.ca_pem="synthetic-trusted-ca";return c;
}
static std::string collection(const std::string& href="/QA/",const std::string& type="<d:collection/>",const std::string& status="HTTP/1.1 200 OK"){
  return "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>"+href+
    "</d:href><d:propstat><d:prop><d:resourcetype>"+type+
    "</d:resourcetype></d:prop><d:status>"+status+"</d:status></d:propstat></d:response></d:multistatus>";
}
static void reset(){assert(!state.liveHTTP&&!state.liveTLS);state={};fakeNow=0;}
static pro::Result probe(CloudConfig& config,bool callback=true){
  WebDAVClient remote(config);
  return remote.probeConnectionReadOnly(callback?+[](void* p){return *static_cast<bool*>(p);}:nullptr,&state.cancelled);
}
static void done(const char* label){
  assert(!state.liveHTTP&&!state.liveTLS&&!state.storageReads&&!state.fileOpens&&!state.transferEvents&&!state.maintenanceReads);
  for(const auto& request:state.received)assert(request.method=="PROPFIND");
  ++passed;std::cout<<"PASS readonly DAV "<<label<<'\n';
}
static void expect(const pro::Result& result,int code){assert(result.code==code&&result.ok==(code==207));}
int main(){
  reset();state.replies={{207,collection()}};
  {auto c=configuration();expect(probe(c),207);}
  assert(state.received.size()==1&&state.begins==1&&state.ends==1);
  const auto& request=state.received.front();assert(request.endpoint=="https://dav.example.test/QA/");
  assert(request.headers.at("Depth")=="0"&&request.headers.at("Content-Type")=="application/xml"&&request.headers.at("Accept-Encoding")=="identity");
  assert(request.headers.at("Cache-Control")=="no-cache, no-store");
  assert(request.body.find("<d:resourcetype/>")!=std::string::npos&&request.body.find("allprop")==std::string::npos);
  done("one authenticated Depth0 PROPFIND; paused backup does not block coordinator probe");

  for(const std::string href:{"https://dav.example.test/QA/","/QA","https://dav.example.test/QA"}){
    reset();state.replies={{207,collection(href)}};auto c=configuration();expect(probe(c,false),207);done("exact resource accepts absolute and directory slash form");
  }
  reset();state.replies={{207,collection("/")}};{auto c=configuration();c.server_url="https://dav.example.test/";expect(probe(c),207);}done("root collection href remains valid");
  reset();auto described=collection();described.insert(described.find("</d:multistatus>"),"<d:responsedescription>Readable</d:responsedescription>");state.replies={{207,described}};
  {auto c=configuration();expect(probe(c),207);}done("optional DAV response description does not change resource proof");
  reset();state.replies={{207,"<multistatus xmlns=\"DAV:\"><response><href>/QA/</href><propstat><prop><resourcetype><collection/></resourcetype></prop><status>HTTP/1.0 200 OK</status></propstat></response></multistatus>"}};
  {auto c=configuration();expect(probe(c),207);}done("default DAV namespace and HTTP1.0 property status remain compatible");
  for(const int code:{401,403,404,301,302,303,307,308,429,500,503,507}){
    reset();state.replies={{code,""}};auto c=configuration();expect(probe(c),code);
    assert(state.received.size()==1&&state.ends==1);done("HTTP auth/permission/missing/redirect/server failure preserved without followup");
  }
  reset();state.replies={{404,""}};{auto c=configuration();c.server_url=KoofrServer;expect(probe(c),404);}
  assert(state.received.size()==1);done("missing standard Koofr folder never invokes MKCOL");
  reset();state.replies={{200,"<html>Accept</html>"}};{auto c=configuration();expect(probe(c),-2011);}done("unexpected successful HTML is not DAV readiness");
  for(const int code:{401,403,404}){
    reset();state.replies={{207,collection("/QA/","",std::string("HTTP/1.1 ")+std::to_string(code)+" denied")}};
    auto c=configuration();expect(probe(c),code);done("inner DAV property authorization failure remains a failure");
  }
  reset();state.replies={{207,"<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/QA/</d:href><d:status>HTTP/1.1 403 Forbidden</d:status></d:response></d:multistatus>"}};
  {auto c=configuration();expect(probe(c),403);}done("direct DAV resource permission failure preserved");

  for(const int mode:{0,1,2}){
    reset();state.replies={{207,collection()}};
    if(mode==0)state.tlsOk=false;if(mode==1)state.hostnameOk=false;
    auto c=configuration();if(mode==2)c.ca_pem="synthetic-wrong-ca";
    expect(probe(c),-1);assert(state.received.empty()&&state.deniedTLS==1);done("bad TLS/hostname/CA receives no authenticated application request");
  }
  reset();state.beginOk=false;{auto c=configuration();expect(probe(c),-2001);}assert(state.received.empty()&&state.ends==1);done("begin failure cleans both scopes");
  reset();state.wifi=0;{auto c=configuration();expect(probe(c),-2302);}assert(!state.begins);done("unavailable Wi-Fi sends nothing");
  reset();state.utc=1767225599;{auto c=configuration();expect(probe(c),-2303);}assert(!state.begins);done("stale clock cannot weaken certificate validation");
  for(const int mode:{0,1,2,3,4,5}){
    reset();auto c=configuration();
    if(mode==0)c.is_valid=false;if(mode==1)c.server_url="http://dav.example.test/QA/";
    if(mode==2)c.server_url="https://synthetic-user@dav.example.test/QA/";
    if(mode==3)c.ca_pem="";if(mode==4)c.username="";if(mode==5)c.password="";
    expect(probe(c),-2301);assert(!state.begins);done("invalid config refuses credentials before TLS allocation");
  }
  reset();state.failBuffer=true;{auto c=configuration();expect(probe(c),-2113);}assert(!state.begins);done("bounded response buffer allocation failure is explicit");

  const auto valid=collection();
  for(const int declared:{-1,4096}){
    reset();const auto body=valid+std::string(4096-valid.size(),' ');state.replies={{207,body,declared}};
    auto c=configuration();expect(probe(c),207);done("exact4096 body supports bounded known/chunked framing");
  }
  for(const int declared:{-1,4097}){
    reset();state.replies={{207,valid+std::string(4097-valid.size(),' '),declared}};
    auto c=configuration();expect(probe(c),-2011);done("declared or streamed oversized response rejected");
  }
  for(const int adjustment:{-1,1}){
    reset();state.replies={{207,valid,int(valid.size())+adjustment}};
    auto c=configuration();expect(probe(c),-2011);done("short/inconsistent declared content length fails closed");
  }
  reset();state.replies={{207,valid,-1,true}};{auto c=configuration();expect(probe(c),-2011);}done("body read error cannot confirm readiness");
  const std::string malformed[]={"","<html>Accept</html>","<d:multistatus>",
    collection("/different/"),collection("https://other.example.test/QA/"),collection("/QA/",""),
    collection("/QA/","<d:collection/><d:unknown/>"),collection("/QA/","<d:collection/><d:collection/>"),
    collection("/QA/","<d:collection/>not-whitespace"),
    collection("/QA/","<d:collection/>","HTTP/1.1 200invalid"),
    "<!DOCTYPE d [<!ENTITY e 'bad'>]>"+valid,
    std::string(valid)+std::string("\0tail",5),valid+valid,
    "<d:multistatus xmlns:d=\"not-DAV\"><d:response/></d:multistatus>"};
  for(const auto& body:malformed){reset();state.replies={{207,body}};auto c=configuration();expect(probe(c),-2011);done("malformed/wrong-origin/noncollection/XML ambiguity rejected");}
  for(const int mode:{0,1,2}){
    reset();auto body=valid;
    if(mode==0){const auto first=body.find("<d:response>"),last=body.find("</d:response>")+13;body.insert(body.find("</d:multistatus>"),body.substr(first,last-first));}
    if(mode==1)body.insert(body.find("</d:prop>"),"<d:resourcetype><d:collection/></d:resourcetype>");
    if(mode==2)body.insert(body.find("</d:response>"),"<d:href>/QA/</d:href>");
    state.replies={{207,body}};auto c=configuration();expect(probe(c),-2011);done("duplicate DAV response/property/href is ambiguous and rejected");
  }
  reset();std::string tags="<d:multistatus xmlns:d=\"DAV:\">";for(unsigned i=0;i<65;++i)tags+="<d:x/>";tags+="</d:multistatus>";state.replies={{207,tags}};
  {auto c=configuration();expect(probe(c),-2011);}done("XML node budget bounds parser allocations");

  for(const int stage:{0,1,2,3}){
    reset();state.replies={{207,valid}};
    if(stage==0)state.cancelled=true;
    if(stage==1)state.afterBegin=[](){state.cancelled=true;};
    if(stage==2)state.afterRequest=[](){state.cancelled=true;};
    if(stage==3)state.duringRead=[](size_t){state.cancelled=true;};
    auto c=configuration();expect(probe(c),pro::SnapshotChanged);done("explicit cancellation at each I/O boundary denies success and cleans scopes");
  }
  reset();state.replies={{207,valid}};
  {auto c=configuration();WebDAVClient remote(c);unsigned closedChecks=0;
   expect(remote.probeConnectionReadOnly(+[](void* context){auto& n=*static_cast<unsigned*>(context);return !state.liveHTTP&&state.attempts&&++n>=2;},&closedChecks),pro::SnapshotChanged);}
  done("late cancellation after TLS cleanup/parser cannot publish success");
  for(const int stage:{0,1,2}){
    reset();state.replies={{207,valid}};
    if(stage==0)state.afterBegin=[](){fakeNow+=30000;};
    if(stage==1)state.afterRequest=[](){fakeNow+=30000;};
    if(stage==2)state.duringRead=[](size_t){fakeNow+=30000;};
    auto c=configuration();expect(probe(c),-2012);done("overall admission/body deadline never becomes readiness");
  }
  reset();fakeNow=UINT32_MAX-1000;state.replies={{207,valid}};state.afterRequest=[](){fakeNow+=30000;};
  {auto c=configuration();expect(probe(c),-2012);}done("probe deadline remains correct across millis rollover");
  std::cout<<passed<<" strict readonly adapter cases passed; no physical TLS/network/storage proof\n";
}
