#include "webdav_client.h"
#include "pro_device.h"
#include "usb_storage.h"
#include <WiFi.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <functional>
#include <limits>
#include <cstdio>
using namespace dav_fixture;
using namespace pro::health;
uint32_t fakeNow=100;
static bool paused=false;
static unsigned sdChecks=0,fileOpens=0,passes=0;
bool maintenance_active(){return paused;}
bool storage_ready(){++sdChecks;return false;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){assert(false&&"health changed backup transfer state");}
void backup_progress(uint64_t,uint64_t){assert(false&&"health changed file progress");}
extern "C" FILE* __wrap_fopen(const char*,const char*){++fileOpens;assert(false&&"health opened filesystem");return nullptr;}
CloudConfig configuration(){CloudConfig c;c.is_valid=true;c.server_url=Base;c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;}
Snapshot snapshot(){Snapshot s;std::strcpy(s.firmware,"0.4.0-product-lab3");s.boot_id=0x1234567890abcdefULL;s.sequence=1;
 s.utc_valid=true;s.reported_at=1793828957;s.uptime_ms=654321;s.wifi=Wifi::Connected;s.wifi_observed_at=s.reported_at;
 s.cloud_check=CloudCheck::ReadAccessOk;s.cloud_checked_at=s.reported_at-60;s.cloud_code=207;s.usb=UsbOwnership::Host;
 s.sd=Sd::Initialized;s.scan=Scan::Partial;s.scan_observed_at=s.reported_at-30;s.pending=3;s.failed=1;s.verified=2;s.last_file_verified_at=s.reported_at-60;return s;}
Admission admitted(){return {true,true,true};}
std::string folder(){auto c=configuration();const auto root=Base+(Base.back()=='/'?"":"/");return root+"._csp_health_v1_"+hash_text("CSP-health-v1\n"+std::to_string(ESP.getEfuseMac())+"\n"+c.server_url.c_str()+"\n"+c.username.c_str())+"/";}
std::string marker(){return folder()+"owner.txt";}
std::string slot(unsigned n){return folder()+(n?"status-b.txt":"status-a.txt");}
void reset(bool keep=false){assert(!liveTLS&&!liveHTTP);requests.clear();forcedStatus.clear();forcedDeclared.clear();forcedBody.clear();readFailures.clear();
 onBegin={};onAfterRequest={};onBeforeRequest={};onRead={};onUpload={};beginCount=0;maxTLS=maxHTTP=0;beginOk=true;connected=true;paused=false;fakeNow=100;sdChecks=fileOpens=0;
 if(!keep){resources.clear();etagSequence=0;resources[Base+"protected.txt"]={false,"synthetic protected data","\"protected\""};}}
Publication publish(const Snapshot& s=snapshot(),Admission a=admitted()){
 auto c=configuration();WebDAVClient client(c);return client.publishHealth(s,a);
}
void invariant(){assert(!liveTLS&&!liveHTTP&&maxTLS<=1&&maxHTTP<=1&&sdChecks==0&&fileOpens==0);
 assert(resources.at(Base+"protected.txt").body=="synthetic protected data");
 for(const auto& request:requests){assert(request.authorized&&request.trusted&&request.redirects_disabled);assert(request.url.rfind(folder(),0)==0);
  assert(request.method=="GET"||request.method=="PROPFIND"||request.method=="MKCOL"||request.method=="PUT");
  if(request.method=="PUT")assert(request.headers.count("If-Match")||request.headers.count("If-None-Match"));}}
void done(const char* name){invariant();++passes;std::cout<<"PASS "<<name<<std::endl;}
void seed(unsigned count=2){for(unsigned i=0;i<count;++i){auto r=publish();assert(r.ok&&r.revision==i+1&&r.slot==i%2);}reset(true);}
bool isSlotPut(const Request& r){return r.method=="PUT"&&(r.url==slot(0)||r.url==slot(1));}
unsigned puts(){unsigned n=0;for(const auto& r:requests)if(isSlotPut(r))++n;return n;}

int main(){
 reset();auto r=publish();assert(r.ok&&r.revision==1&&r.slot==0&&requests.size()==8&&puts()==1);
 assert(resources.at(marker()).body.rfind("CSP-HEALTH-OWNER/1\n",0)==0);
 assert(resources.at(slot(0)).body.find("Last reported snapshot; silence means unknown")!=std::string::npos);
 assert(resources.at(slot(0)).body.find("Last file verified UTC:")!=std::string::npos);
 assert(requests[2].headers.at("If-None-Match")=="*"&&requests[6].headers.at("If-None-Match")=="*");
 done("first publication owns namespace and verifies RAM body without SD");

 reset(true);r=publish();assert(r.ok&&r.revision==2&&r.slot==1&&puts()==1);done("second publication uses other slot");
 const auto oldB=resources.at(slot(1)).body;const auto oldTag=resources.at(slot(0)).etag;
 reset(true);r=publish();assert(r.ok&&r.revision==3&&r.slot==0&&resources.at(slot(1)).body==oldB);
 bool conditional=false;for(const auto& q:requests)if(isSlotPut(q))conditional=q.headers.at("If-Match")==oldTag;
 assert(conditional);done("later publication replaces older owned slot with strong If-Match");

 for(unsigned count=0;count<4;++count){reset();auto a=admitted();if(count==0)a.normal_worker=false;if(count==1)a.sd_view_unmounted=false;if(count==2)a.radio_exclusive=false;if(count==3)paused=true;
  r=publish(snapshot(),a);assert(!r.ok&&r.code==HealthDenied&&requests.empty()&&beginCount==0);done("denied admission sends no request");}
 for(unsigned bad=0;bad<3;++bad){reset();auto s=snapshot();if(bad==0)s.firmware[1]='\n';if(bad==1)s.boot_id=0;if(bad==2)s.utc_valid=false;
  r=publish(s);assert(!r.ok&&r.code==HealthInvalid&&requests.empty()&&beginCount==0);done("invalid snapshot sends no request");}
 reset();{auto c=configuration();c.is_valid=false;WebDAVClient client(c);r=client.publishHealth(snapshot(),admitted());}
 assert(!r.ok&&requests.empty()&&beginCount==0);done("invalid cloud configuration sends no request");
 reset();connected=false;r=publish();assert(!r.ok&&requests.empty());done("disconnected Wi-Fi sends no request");
 reset();{auto c=configuration();c.ca_pem="";WebDAVClient client(c);r=client.publishHealth(snapshot(),admitted());}
 assert(!r.ok&&requests.empty());done("missing trusted CA never sends request");
 reset();beginOk=false;r=publish();assert(!r.ok&&requests.empty());done("TLS begin failure never sends request");

 reset();resources[folder()]={true,{},"\"foreign\""};r=publish();assert(!r.ok&&r.code==HealthCollision&&requests.size()==2&&puts()==0&&!resources.count(marker()));done("existing unmarked namespace is never claimed");
 reset();resources[marker()]={false,"CSP-HEALTH-OWNER/1\n"+std::string(64,'0')+"\n","\"foreign\""};r=publish();assert(!r.ok&&r.code==HealthCollision&&requests.size()==1&&puts()==0);done("foreign marker collision is terminal for health only");
 reset();resources[marker()]={true,{},"\"collection\""};r=publish();assert(!r.ok&&puts()==0&&resources.at(marker()).collection);done("marker collection never overwritten");
 reset();onBeforeRequest=[](unsigned,const Request& q){if(q.method=="PUT"&&q.url==marker())resources[marker()]={false,"raced foreign marker","\"race\""};};
 r=publish();assert(!r.ok&&r.code==412&&resources.at(marker()).body=="raced foreign marker"&&puts()==0);done("marker create race preserves foreign data");

 reset();seed();resources[slot(0)]={true,{},"\"collection\""};r=publish();assert(!r.ok&&puts()==0&&resources.at(slot(0)).collection);done("slot collection rejected before PUT");
 for(const std::string& tag:{std::string("W/\"weak\""),std::string("\"bad[tag]\""),std::string("\"bad tag\""),std::string("\"")+std::string(130,'x')+"\""}){
  reset();seed();resources[slot(0)].etag=tag;r=publish();assert(!r.ok&&puts()==0);done("weak unsafe or oversized validator rejected");}

 for(unsigned kind=0;kind<4;++kind){reset();seed();const auto old=resources.at(slot(0));
  if(kind==0)resources[slot(0)].body="unknown schema";
  if(kind==1){auto& body=resources[slot(0)].body;body.back()^=1;}
  if(kind==2)resources[slot(0)].body+="trailing";
  if(kind==3)resources[slot(0)].body=resources[slot(0)].body.substr(0,20);
  const auto damaged=resources.at(slot(0)).body;r=publish();assert(!r.ok&&puts()==0&&resources.at(slot(0)).body==damaged);
  done(kind==0?"unknown slot schema never overwritten":kind==1?"owned full-body corruption rejected":kind==2?"owned trailing bytes rejected":"partial unowned header rejected");}
 reset();seed();{auto& body=resources[slot(0)].body;body.resize(body.find("\n\n")+2+5);}
 {auto preserved=resources.at(slot(1)).body;r=publish();assert(r.ok&&r.revision==3&&r.slot==0&&resources.at(slot(1)).body==preserved);}
 done("owned complete-header truncated body repairs conditionally while other slot remains");
 reset();seed();resources[slot(1)].body=resources[slot(0)].body;r=publish();assert(!r.ok&&r.code==HealthCollision&&puts()==0);done("equal revision replay ambiguity rejected");

 for(int code:{401,403,307,412,507}){reset();forcedStatus[1]=code;r=publish();assert(!r.ok&&r.code==code&&requests.size()==1);done("marker auth redirect conditional quota failure preserved");}
 for(int code:{401,403,307,412,507}){reset();seed();onBeforeRequest=[code](unsigned n,const Request& q){if(isSlotPut(q))forcedStatus[n]=code;};
  const auto oldA=resources.at(slot(0)).body;const auto oldB2=resources.at(slot(1)).body;r=publish();assert(!r.ok&&r.code==code&&resources.at(slot(0)).body==oldA&&resources.at(slot(1)).body==oldB2);
  done("slot PUT auth redirect conditional quota failure preserved");}
 reset();seed(1);{const auto old=resources.at(slot(0)).body;
  onBeforeRequest=[](unsigned n,const Request& q){if(isSlotPut(q)){assert(q.url==slot(1)&&q.headers.at("If-None-Match")=="*");forcedStatus[n]=204;}};
  r=publish();assert(!r.ok&&r.code==HealthCollision&&puts()==1&&requests.back().method=="PUT"&&!resources.count(slot(1))&&resources.at(slot(0)).body==old);}
 done("missing-slot conditional create rejects ambiguous 204 without acknowledgment");
 reset();seed();{const auto oldA=resources.at(slot(0)).body,oldB2=resources.at(slot(1)).body;
  onBeforeRequest=[](unsigned n,const Request& q){if(isSlotPut(q)){assert(q.url==slot(0)&&q.headers.count("If-Match"));forcedStatus[n]=201;}};
  r=publish();assert(!r.ok&&r.code==HealthCollision&&puts()==1&&requests.back().method=="PUT"&&resources.at(slot(0)).body==oldA&&resources.at(slot(1)).body==oldB2);}
 done("existing-slot conditional replacement rejects ambiguous 201 without acknowledgment");
 reset();seed();onBeforeRequest=[](unsigned,const Request& q){if(isSlotPut(q)){resources[q.url].etag="\"race\"";resources[q.url].body="concurrent owned change";}};
 r=publish();assert(!r.ok&&r.code==412&&resources.at(slot(0)).body=="concurrent owned change");done("strong conditional replacement race cannot overwrite changed slot");

 for(unsigned stop=1;stop<=8;++stop){reset();onAfterRequest=[stop](unsigned n){if(n==stop)paused=true;};r=publish();assert(!r.ok&&r.code==pro::SnapshotChanged&&requests.size()==stop);done("retirement after each request stops chain");}
 for(unsigned stop=1;stop<=8;++stop){reset();onBegin=[stop](unsigned n){if(n==stop)paused=true;};r=publish();assert(!r.ok&&r.code==pro::SnapshotChanged&&requests.size()==stop-1);done("retirement after each TLS begin sends no next request");}
 for(unsigned which:{4U,8U}){reset();onRead=[which](unsigned n,size_t at){if(n==which&&at>=16)paused=true;};r=publish();assert(!r.ok&&r.code==pro::SnapshotChanged&&requests.size()==which);done("retirement during marker or slot readback cannot verify");}
 for(size_t at:{size_t(16),size_t(256)}){reset();seed();onUpload=[at](unsigned,size_t done){if(done>=at)paused=true;};r=publish();assert(!r.ok&&r.code==pro::SnapshotChanged&&puts()==1);done("retirement during RAM stream cannot acknowledge publication");}
 reset();onAfterRequest=[](unsigned){fakeNow+=30000;};r=publish();assert(!r.ok&&r.code==HealthDeadline&&requests.size()==1);done("30-second attempt budget halts next request");
 reset();fakeNow=UINT32_MAX-100;onAfterRequest=[](unsigned){fakeNow+=30000;};r=publish();assert(!r.ok&&r.code==HealthDeadline&&requests.size()==1);done("attempt budget remains correct across millis wrap");
 reset();seed();readFailures.insert(3);r=publish();assert(!r.ok&&puts()==0);done("short failed slot read cannot publish");
 reset();seed();forcedDeclared[3]=int(1024+224+1);r=publish();assert(!r.ok&&r.code==HealthMalformed&&puts()==0);done("oversized declared slot rejected");
 reset();seed();onBeforeRequest=[](unsigned n,const Request& q){if(q.method=="GET"&&q.url==slot(0)&&puts()==1)forcedBody[n]="truncated final readback";};
 r=publish();assert(!r.ok&&r.code==HealthMalformed);done("final readback mismatch never acknowledges success");
 reset(true);r=publish();assert(r.ok&&r.revision==4);done("retry discovers previously committed but unacknowledged slot without false local success");
 reset();seed();onBeforeRequest=[](unsigned n,const Request& q){if(isSlotPut(q))forcedStatus[n]=507;};r=publish();assert(!r.ok&&r.code==507);
 reset(true);r=publish();assert(r.ok&&r.revision==3);done("owned namespace retry after quota failure preserves prior reports");
 reset();onBeforeRequest=[](unsigned n,const Request& q){if(q.method=="GET"&&q.url==slot(0)&&puts()==1)forcedDeclared[n]=int(resources.at(q.url).body.size()+1);};
 r=publish();assert(!r.ok&&r.code==HealthMalformed);done("short declared HTTP final readback cannot acknowledge success");
 for(int difference:{-1,1}){reset();seed();forcedDeclared[1]=int(resources.at(marker()).body.size())+difference;
  r=publish();assert(!r.ok&&puts()==0);done("marker declared-size mismatch rejects before namespace use");}
 for(int difference:{-1,1}){reset();seed();forcedDeclared[3]=int(resources.at(slot(0)).body.size())+difference;
  r=publish();assert(!r.ok&&r.code==HealthMalformed&&puts()==0);done("existing slot HTTP framing mismatch cannot trigger overwrite or repair");}
 reset();seed();onBeforeRequest=[](unsigned n,const Request& q){if(q.method=="GET")forcedDeclared[n]=-1;};
 r=publish();assert(r.ok&&r.revision==3);done("unknown chunked length remains bounded by complete envelope and hash");
 reset();forcedStatus[3]=507;r=publish();assert(!r.ok&&r.code==507&&!resources.count(marker()));
 reset(true);r=publish();assert(!r.ok&&r.code==HealthCollision&&puts()==0);done("failed ownership initialization leaves unmarked namespace fail-closed");
 for(bool ownerField:{true,false}){reset();seed();auto& body=resources[slot(0)].body;
  const auto at=ownerField?body.find("\nrevision="):body.find("\n\n");assert(at!=std::string::npos);
  body.insert(at,std::string("\0hidden",7));const auto before=body;r=publish();assert(!r.ok&&puts()==0&&resources.at(slot(0)).body==before);
  done(ownerField?"embedded NUL after owner cannot hide extra header bytes":"embedded NUL after hash cannot hide extra header bytes");}
 reset();seed();onUpload=[](unsigned,size_t done){if(done>=256)fakeNow+=30000;};r=publish();assert(!r.ok&&r.code==HealthDeadline&&puts()==1);done("attempt deadline interrupts RAM upload without acknowledgment");
 reset();onRead=[](unsigned n,size_t at){if(n==8&&at>=256)fakeNow+=30000;};r=publish();assert(!r.ok&&r.code==HealthDeadline);done("attempt deadline interrupts final streamed verification");
 std::cout<<passes<<" cases passed; actual publisher/DAV/model, synthetic HTTP/TLS only\n";
 return 0;
}
