// Reproduce RC4 behavior without changing the actual publisher. Synthetic DAV.
#define main original_publisher_suite_main
#include "publisher.test.cpp"
#undef main

int main(){
  reset();seed();const auto priorA=resources.at(slot(0)).body;
  const auto priorB=resources.at(slot(1)).body;
  const auto oldTag=resources.at(slot(0)).etag;
  successfulReplacementStatus=201;
  auto r=publish();
  assert(!r.ok&&r.code==HealthCollision&&r.revision==0&&puts()==1);
  assert(requests.back().method=="PUT"&&requests.back().headers.at("If-Match")==oldTag);
  assert(resources.at(slot(0)).body!=priorA&&resources.at(slot(1)).body==priorB);
  // Independently construct the full canonical body from the real encoder/hash.
  auto s=snapshot();s.sequence=3;char payload[ReportCapacity];size_t length=0;
  assert(encode(s,payload,sizeof payload,length));
  const auto scope=hash_text("CSP-health-v1\n"+std::to_string(ESP.getEfuseMac())+"\n"+Base+"\nsynthetic-user");
  const std::string report(payload,length);
  const auto expected="CSP-HEALTH/1\nowner="+scope+"\nrevision=3\nlength="+
    std::to_string(length)+"\nsha256="+hash_text(report)+"\n\n"+report;
  assert(resources.at(slot(0)).body==expected);
  done("RC4201 after conditional write rejects complete owned body before final GET");

  reset(true);successfulReplacementStatus=204;r=publish();
  assert(r.ok&&r.revision==4&&r.slot==1&&resources.at(slot(0)).body==expected);
  done("next actual publisher read accepts previously written owned revision3");

  reset();seed();successfulReplacementStatus=201;
  for(unsigned revision=3;revision<=5;++revision){
    r=publish();assert(!r.ok&&r.code==HealthCollision&&requests.back().method=="PUT");
    const auto& body=resources.at(slot((revision-1)%2)).body;
    assert(body.find("\nrevision="+std::to_string(revision)+"\n")!=std::string::npos);
    reset(true);
  }
  done("repeated201 writes advance remote slots while every local acknowledgment fails");
  successfulReplacementStatus=204;
  std::cout<<"3 synthetic201 reproduction cases passed; production source unchanged\n";
}
