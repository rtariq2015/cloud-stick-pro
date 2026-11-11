#include <ctime>
#include <iostream>
#include <cassert>
#include <functional>
static time_t testEpoch=1794355200;
static time_t fake_time(time_t* out){if(out)*out=testEpoch;return testEpoch;}
#define time fake_time
#include "../../src/guest_internet_probe.cpp"
#undef time
using namespace pro::guest_diag;
static unsigned cases=0,checks=0;
#define CHECK(value) do {++checks;if(!(value)){std::cerr<<"failed line "<<__LINE__<<": " #value "\n";std::abort();}} while(false)
static void reset(){
 CHECK(!internetBusy());CHECK(allocations.empty());CHECK(dnsAllocations==dnsFrees);
 owner=nullptr;task=nullptr;stack=nullptr;control=nullptr;core=-1;cancelled=false;handoff=false;drained=false;result={};
 currentTask=&mainTask;currentCore=0;testMillis=0;testEpoch=1794355200;WiFi.state=WL_CONNECTED;
 freeBytes=240000;largestBytes=150000;allocationCalls=0;failAllocation=0;createTaskFail=false;deletedTasks=0;taskEntry=nullptr;
 delayHook={};dnsHook={};socketConnectHook={};socketReadHook={};socketWriteHook={};dnsHttp=dnsHttps=true;dnsQueries.clear();requests.clear();
 plainPlan={true,0,"HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\nDO_NOT_READ_BODY"};securePlan={};
}
static InternetResult complete(){
 CHECK(internetStart());CHECK(internetBusy());CHECK(!internetStart());InternetResult out;
 CHECK(!internetReap(out));runTask();CHECK(internetBusy());CHECK(internetReap(out));CHECK(!internetBusy());
 CHECK(!internetReap(out));CHECK(allocations.empty());CHECK(deletedTasks==1);return out;
}
static void test(const char* name,const std::function<void()>& fn){reset();fn();CHECK(!internetBusy());++cases;std::cout<<"PASS "<<name<<"\n";}
int main(){
 test("fixed credential-free GET requests with verified HTTPS configuration",[]{
  const auto out=complete();CHECK(out.dns_http&&out.dns_https&&out.clock_valid&&out.https_verified);CHECK(out.http_status==204&&out.https_status==200);
  CHECK(dnsQueries==std::vector<std::string>({"connectivitycheck.gstatic.com","www.google.com"}));CHECK(requests.size()==2);
  CHECK(!requests[0].secure&&requests[0].hostname=="resolved IPv4"&&requests[0].port==80);CHECK(requests[1].secure&&requests[1].hostname=="www.google.com"&&requests[1].port==443);
  CHECK(requests[0].request=="GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\nConnection: close\r\n\r\n");
  CHECK(requests[1].request=="GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n");
  CHECK(!requests[0].bundle);CHECK(requests[1].bundle&&!requests[1].insecure&&requests[1].handshakeTimeout==5);
  for(const auto& r:requests){CHECK(r.connectTimeout==5000);CHECK(r.stopped&&r.stops==1);CHECK(r.request.find("Authorization")==std::string::npos);CHECK(r.request.find("Cookie")==std::string::npos);CHECK(r.request.find("Content-Length")==std::string::npos);}
  CHECK(requests[0].readBytes==plainPlan.response.find("\r\n\r\n")+4);CHECK(requests[1].readBytes==securePlan.response.find("\r\n\r\n")+4);
 });
 test("HTTP redirect captured without following or reading body",[]{plainPlan.response="HTTP/1.1 302 Found\r\nLocation: https://portal.invalid/private\r\nSet-Cookie: secret=value\r\n\r\nDO_NOT_READ";const auto out=complete();CHECK(out.http_status==302);CHECK(out.https_verified);CHECK(requests.size()==2);CHECK(requests[0].readBytes==plainPlan.response.find("\r\n\r\n")+4);});
 test("unexpected HTTP page captured as status only",[]{plainPlan.response="HTTP/1.1 200 OK\r\nContent-Length: 1000000\r\n\r\nDO_NOT_READ";const auto out=complete();CHECK(out.http_status==200);CHECK(requests[0].readBytes==plainPlan.response.find("\r\n\r\n")+4);});
 test("DNS failure prevents corresponding HTTP and TLS connections",[]{dnsHttp=dnsHttps=false;const auto out=complete();CHECK(!out.dns_http&&!out.dns_https&&!out.https_verified);CHECK(out.http_status==0&&out.https_status==0);CHECK(requests.empty());});
 test("HTTP DNS failure still allows independent HTTPS diagnosis",[]{dnsHttp=false;const auto out=complete();CHECK(!out.dns_http&&out.dns_https&&out.https_verified);CHECK(requests.size()==1);});
 test("invalid clock prevents TLS rather than disabling certificate checks",[]{testEpoch=0;const auto out=complete();CHECK(!out.clock_valid&&!out.https_verified&&out.https_status==0);CHECK(testMillis==20000);CHECK(requests.size()==1);});
 test("clock synchronization within wait allows TLS",[]{testEpoch=0;delayHook=[]{if(testMillis>=3000)testEpoch=1794355200;};const auto out=complete();CHECK(out.clock_valid&&out.https_verified);CHECK(testMillis==3000);});
 test("clock wait handles uint32 rollover",[]{testEpoch=0;testMillis=UINT32_MAX-500;const auto before=testMillis;const auto out=complete();CHECK(!out.clock_valid);CHECK(uint32_t(testMillis-before)==20000);});
 test("certificate failure preserves TLS error and never claims verified",[]{securePlan.connectOk=false;securePlan.tlsError=-0x2700;const auto out=complete();CHECK(!out.https_verified&&out.https_status==0);CHECK(out.tls_error==-0x2700);});
 test("TLS error rejects anomalous successful transport",[]{securePlan.tlsError=-0x2700;const auto out=complete();CHECK(!out.https_verified);});
 test("HTTP connection failure does not suppress independent TLS check",[]{plainPlan.connectOk=false;const auto out=complete();CHECK(out.http_status==0&&out.http_error==ESP_FAIL);CHECK(out.https_verified);});
 test("oversized response headers stop at exact byte budget",[]{securePlan.response="HTTP/1.1 200 OK\r\nX: "+std::string(5000,'x')+"\r\n\r\n";const auto out=complete();CHECK(out.https_error==ESP_ERR_INVALID_SIZE);CHECK(out.https_status==0&&!out.https_verified);CHECK(requests[1].readBytes==4096);});
 test("headers ending exactly at byte budget remain valid",[]{const std::string prefix="HTTP/1.1 200 OK\r\nX: ",suffix="\r\n\r\n";securePlan.response=prefix+std::string(4096-prefix.size()-suffix.size(),'x')+suffix+"BODY";const auto out=complete();CHECK(out.https_verified);CHECK(requests[1].readBytes==4096);});
 test("overlong status line is rejected before header budget",[]{securePlan.response="HTTP/1.1 200 "+std::string(100,'x')+"\r\n\r\n";const auto out=complete();CHECK(out.https_error==ESP_ERR_INVALID_SIZE&&!out.https_verified);CHECK(requests[1].readBytes==96);});
 test("slow header stream cannot extend whole-header deadline",[]{plainPlan.byteIntervalMs=1000;const auto out=complete();CHECK(out.http_status==0&&out.http_error==ESP_ERR_TIMEOUT);CHECK(testMillis==5000);CHECK(requests[0].readBytes==5);CHECK(out.https_verified);});
 test("whole-header deadline handles uint32 rollover",[]{testMillis=UINT32_MAX-50;const auto before=testMillis;plainPlan.byteIntervalMs=1000;const auto out=complete();CHECK(out.http_error==ESP_ERR_TIMEOUT);CHECK(uint32_t(testMillis-before)==5000);});
 test("incomplete header and closed socket fail immediately",[]{securePlan.response="HTTP/1.1 200 OK\r\nIncomplete: value";const auto out=complete();CHECK(out.https_error==ESP_FAIL&&!out.https_verified);CHECK(testMillis==0);});
 test("idle open socket hits deadline",[]{securePlan.response="";securePlan.holdOpen=true;const auto out=complete();CHECK(out.https_error==ESP_ERR_TIMEOUT&&!out.https_verified);CHECK(testMillis==5000);});
 test("malformed status lines never verify",[]{securePlan.response="HTTP/2 200 OK\r\n\r\n";const auto out=complete();CHECK(out.https_error==ESP_FAIL&&!out.https_verified);});
 test("nondigit response status never verifies",[]{securePlan.response="HTTP/1.1 2x0 OK\r\n\r\n";const auto out=complete();CHECK(out.https_error==ESP_FAIL&&!out.https_verified);});
 test("partial request write fails instead of parsing unrelated response",[]{securePlan.writeLimit=10;const auto out=complete();CHECK(out.https_error==ESP_FAIL&&!out.https_verified);CHECK(requests[1].readBytes==0);});
 test("read error closes socket and never verifies",[]{securePlan.readErrorAt=4;const auto out=complete();CHECK(out.https_error==ESP_FAIL&&!out.https_verified);CHECK(requests[1].readBytes==4);});
 test("insufficient total memory prevents probe allocation",[]{freeBytes=96*1024-1;CHECK(!internetStart());CHECK(allocationCalls==0);});
 test("insufficient contiguous memory prevents probe allocation",[]{largestBytes=64*1024-1;CHECK(!internetStart());CHECK(allocationCalls==0);});
 test("stack allocation failure frees partial task resources",[]{failAllocation=1;CHECK(!internetStart());CHECK(allocations.empty());});
 test("task control allocation failure frees partial resources",[]{failAllocation=2;CHECK(!internetStart());CHECK(allocations.empty());});
 test("task creation failure frees all resources",[]{createTaskFail=true;CHECK(!internetStart());CHECK(allocations.empty());});
 test("owner identity and core constrain cancellation and reaping",[]{
  CHECK(internetStart());currentTask=&otherTask;internetCancel();CHECK(!cancelled.load());CHECK(!internetStart());InternetResult out;CHECK(!internetReap(out));
  currentTask=&mainTask;runTask();currentCore=1;CHECK(!internetReap(out));CHECK(deletedTasks==0);currentCore=0;CHECK(internetReap(out));
 });
 test("cancel before task entry never performs network activity",[]{CHECK(internetStart());internetCancel();InternetResult out;CHECK(!internetReap(out));CHECK(deletedTasks==0);runTask();CHECK(internetReap(out));CHECK(dnsQueries.empty()&&requests.empty());});
 test("cancel during DNS waits for callback return and task suspension",[]{
  CHECK(internetStart());dnsHook=[]{cancelled=true;};InternetResult out;CHECK(!internetReap(out));runTask();CHECK(internetReap(out));CHECK(dnsQueries.size()==1&&requests.empty());
 });
 test("cancel during HTTP closes client and skips later network stages",[]{CHECK(internetStart());socketConnectHook=[]{cancelled=true;};runTask();InternetResult out;CHECK(internetReap(out));CHECK(requests.size()==1&&requests[0].stopped);CHECK(!out.https_verified);});
 test("disconnect during headers prevents status acceptance",[]{CHECK(internetStart());socketReadHook=[]{WiFi.state=0;};runTask();InternetResult out;CHECK(internetReap(out));CHECK(out.http_status==0&&!out.https_verified);CHECK(requests.size()==1);});
 test("drained publication alone cannot free an unsuspended live task",[]{CHECK(internetStart());drained=true;InternetResult out;CHECK(!internetReap(out));CHECK(deletedTasks==0);runTask();CHECK(internetReap(out));});
 test("repeat probes reuse ownership without stale completion or cancellation",[]{auto first=complete();CHECK(first.https_verified);CHECK(internetStart());InternetResult out;CHECK(!internetReap(out));runTask();CHECK(internetReap(out));CHECK(out.https_verified);CHECK(deletedTasks==2);});
 reset();std::cout<<"PASS "<<cases<<" actual-source probe scenarios, "<<checks<<" assertions; SDK/network/RTOS simulated\n";
}
