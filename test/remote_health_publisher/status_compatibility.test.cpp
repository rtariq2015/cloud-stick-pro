// Actual publisher/DAV/model; synthetic Koofr HTTP semantics, no real network.
#define main original_publisher_suite_main
#include "publisher.test.cpp"
#undef main

namespace {
const std::string Canonical="https://app.koofr.net/dav/Koofr/SYNTHETIC_HEALTH/";
void setup(const std::string& base=Canonical){
  Base=base;successfulReplacementStatus=204;enforceConditions=true;reset();seed();
  successfulReplacementStatus=201;enforceConditions=false;
}
void checkReadbackFailure(unsigned mode){
  setup();const auto unchanged=resources.at(slot(1)).body;
  onBeforeRequest=[mode](unsigned n,const Request& q){
    if(q.method!="GET"||q.url!=slot(0)||puts()!=1)return;
    if(mode==0){auto body=resources.at(q.url).body;const auto pos=body.find("owner=")+6;body[pos]=body[pos]=='a'?'b':'a';forcedBody[n]=body;}
    if(mode==1){auto body=resources.at(q.url).body;body.back()^=1;forcedBody[n]=body;}
    if(mode==2)readFailures.insert(n);
    if(mode==3)forcedDeclared[n]=int(resources.at(q.url).body.size()+1);
    if(mode==4){auto body=resources.at(q.url).body;const auto pos=body.find("revision=")+9;body[pos]='9';forcedBody[n]=body;}
    if(mode==5)forcedBody[n]=resources.at(q.url).body+"trailing";
  };
  const auto r=publish();assert(!r.ok&&r.code==HealthMalformed&&r.revision==0&&puts()==1);
  assert(resources.at(slot(1)).body==unchanged);
}
}
int main(){
  setup();const auto old=resources.at(slot(0)).etag,other=resources.at(slot(1)).body;
  auto r=publish();assert(r.ok&&r.revision==3&&r.slot==0&&puts()==1);
  assert(requests.back().method=="GET"&&resources.at(slot(0)).etag!=old&&resources.at(slot(1)).body==other);
  bool condition=false;for(const auto& q:requests)if(isSlotPut(q))condition=q.headers.at("If-Match")==old;
  assert(condition);done("canonical Koofr201 requires complete owned revision/hash readback");
  for(unsigned mode=0;mode<6;++mode){checkReadbackFailure(mode);done("Koofr201 wrong owner hash framing revision or body cannot acknowledge");}

  setup();onBeforeRequest=[](unsigned n,const Request& q){if(isSlotPut(q))forcedStatus[n]=201;};
  r=publish();assert(!r.ok&&r.code==HealthMalformed&&r.revision==0);
  done("Koofr201 without applied bytes cannot acknowledge old matching slot");

  setup();onBeforeRequest=[](unsigned n,const Request& q){if(isSlotPut(q))forcedStatus[n]=202;};
  r=publish();assert(!r.ok&&r.code==HealthCollision&&requests.back().method=="PUT");
  done("canonical provider does not accept other ambiguous2xx");

  for(const std::string& base:{std::string("https://dav.example.test/QA/"),
      std::string("https://app.koofr.net.evil.invalid/dav/Koofr/QA/"),
      std::string("https://app.koofr.net:443/dav/Koofr/QA/"),
      std::string("https://app.koofr.net/dav/Koofr/"),
      std::string("https://app.koofr.net/dav/Koofr/QA"),
      std::string("https://app.koofr.net/dav/Koofr//QA/"),
      std::string("https://app.koofr.net/dav/Koofr/../QA/"),
      std::string("https://app.koofr.net/dav/Koofr/QA/?query"),
      std::string("https://app.koofr.net/dav/Koofr/QA/%2f/"),
      std::string("https://app.koofr.net/dav/Koofr/QA/#fragment")}){
    setup(base);r=publish();assert(!r.ok&&r.code==HealthCollision&&requests.back().method=="PUT");
    done("noncanonical provider/path retains strict existing-slot status policy");
  }
  Base="http://app.koofr.net/dav/Koofr/QA/";reset();r=publish();assert(!r.ok&&r.code==HealthInvalid&&requests.empty());
  done("plain HTTP remains denied before provider compatibility or network access");
  setup("https://app.koofr.net/dav/Koofr/Folder-A/Sub_2.3/");r=publish();assert(r.ok&&r.revision==3);
  done("canonical nested safe folder supports provider-specific201");

  setup();const auto prior=resources.at(slot(0));
  onBeforeRequest=[](unsigned,const Request& q){if(isSlotPut(q)){resources[q.url].etag="\"concurrent\"";resources[q.url].body="remote concurrent edit";}};
  r=publish();assert(r.ok&&r.revision==3&&resources.at(slot(0)).body!=prior.body&&resources.at(slot(0)).body!="remote concurrent edit");
  done("documented single-publisher limit ignores stale condition and overwrites concurrent health edit");

  setup();resources[slot(0)].body="foreign malformed report";r=publish();assert(!r.ok&&puts()==0);
  done("preexisting foreign or malformed slot still prevents health write");
  setup();{auto& body=resources[marker()].body;const auto at=body.find('\n')+1;body[at]=body[at]=='a'?'b':'a';}
  r=publish();assert(!r.ok&&r.code==HealthCollision&&puts()==0);
  done("preexisting foreign ownership marker still prevents health write");
  setup();onUpload=[](unsigned,size_t done){if(done>=256)fakeNow+=30000;};r=publish();assert(!r.ok&&r.code==HealthDeadline);
  done("Koofr201 compatibility preserves upload deadline cancellation");
  setup();onRead=[](unsigned,size_t at){if(puts()==1&&at>=256)fakeNow+=30000;};r=publish();assert(!r.ok&&r.code==HealthDeadline);
  done("Koofr201 compatibility preserves readback deadline cancellation");
  successfulReplacementStatus=204;enforceConditions=true;
  std::cout<<passes<<" provider compatibility cases passed; single publisher only; no CAS claim\n";
}
