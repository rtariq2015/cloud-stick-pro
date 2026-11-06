#include "stubs/platform.h"
#include "owned_guest_route.h"
#include <iostream>
#include <random>
using namespace pro::owned_guest;
namespace {
unsigned groups=0;
Request request(){return {"synthetic-ap","synthetic-private-key",{route_fake::ip,route_fake::mask,route_fake::gateway,route_fake::dns},300000};}
void tick(unsigned delta=10){route_fake::now+=delta;route_fake::pump();poll(route_fake::now);route_fake::pump();}
void drive(Phase wanted){for(unsigned i=0;i<1000&&status().phase!=wanted;++i)tick(10);assert(status().phase==wanted);}
void started(){assert(start(request()));assert(!route_fake::napt);drive(Phase::Active);assert(route_fake::napt&&route_fake::sockets.size()==2);}
void stopped(){assert(!stop());drive(Phase::Stopped);assert(stop());
  assert(!route_fake::napt&&route_fake::mode==WIFI_MODE_STA&&route_fake::connected);
  assert(route_fake::sockets.empty()&&route_fake::buffers.empty()&&route_fake::handlers.empty());
  assert(!status().resources_retained&&status().station_preserved);
}
void pass(const char* name){++groups;std::cout<<"PASS "<<name<<'\n';}
int apFd(){for(const auto& item:route_fake::sockets)if(item.second.port==53)return item.first;assert(false);return -1;}
int upFd(){for(const auto& item:route_fake::sockets)if(item.second.port==0)return item.first;assert(false);return -1;}
std::vector<uint8_t> query(uint16_t id){
  return {uint8_t(id>>8),uint8_t(id),1,0,0,1,0,0,0,0,0,0,1,'x',0,0,1,0,1};
}
void submit(int fd,uint32_t ip,uint16_t port,std::vector<uint8_t> bytes){route_fake::sockets.at(fd).incoming.push_back({ip,port,std::move(bytes)});}
void faults(const std::string& mode){
  if(mode=="registration"){
    route_fake::failRegisterAt=2;assert(!start(request()));assert(!route_fake::napt);drive(Phase::Blocked);
  }else{
    started();
    if(mode=="disable")route_fake::naptDisableFail=true;
    else if(mode=="apstop")route_fake::apStopFail=true;
    else if(mode=="stop-event")route_fake::suppressStop=true;
    else if(mode=="barrier")route_fake::failPostAt=route_fake::posts+1;
    else if(mode=="unregister")route_fake::unregisterFail=true;
    else if(mode=="close")route_fake::failCloseFd=apFd();
    else assert(false);
    assert(!stop());drive(Phase::Blocked);
  }
  assert(status().resources_retained&&!route_fake::buffers.empty()&&!stop()&&!start(request()));
  const auto closes=route_fake::closeCalls;const auto ends=route_fake::ends;
  for(unsigned i=0;i<10;++i){tick();assert(!stop());}
  assert(route_fake::closeCalls==closes&&route_fake::ends==ends);
  for(const auto& close:closes)assert(close.second==1);
  assert(status().error!=Error::None);
  if(mode!="apstop")assert(route_fake::mode==WIFI_MODE_STA);
  // Intentional terminal fault retains globally owned resources, not a false
  // successful shutdown. No forced descriptor retry, reset or fake memory proof.
  std::cout<<"PASS terminal "<<mode<<" failure retains cleanup ownership and rejects reuse\n";
}
}
int main(int argc,char** argv){
  if(argc==2){faults(argv[1]);return 0;}
  auto invalid=request();invalid.ap_password="short";assert(!start(invalid));
  invalid=request();invalid.ap_name=std::string_view("bad\0name",8);assert(!start(invalid));
  invalid=request();invalid.uplink.dns=0;assert(!start(invalid));
  invalid=request();invalid.window_ms=300001;assert(!start(invalid));
  invalid=request();invalid.uplink.ip+=1;assert(!start(invalid));
  assert(route_fake::buffers.empty()&&route_fake::creates==0);
  pass("invalid credentials/window/unicast/snapshot rejected before allocation or AP");

  route_fake::allocationFail=true;assert(!start(request()));route_fake::allocationFail=false;
  assert(status().phase==Phase::Stopped&&status().error==Error::Allocation&&route_fake::buffers.empty());
  pass("allocation failure admits no route resources");

  route_fake::ip=0xc0a80002;route_fake::mask=0x80000000;route_fake::gateway=0xc0a80001;route_fake::dns=0x0afdfe05;
  assert(!start(request())&&status().error==Error::NoSubnet);
  route_fake::ip=0x0a000002;route_fake::mask=0xffffff00;route_fake::gateway=0x0a000001;route_fake::dns=0x08080808;
  started();assert(status().ap_ip==0xc0a80401&&route_fake::deauthCalls==1);
  assert(route_fake::config.ap.max_connection==1&&route_fake::config.ap.authmode==WIFI_AUTH_WPA2_PSK);
  assert(!start(request()));stopped();
  pass("nonoverlap choice/private-WPA2 readback/deauth/single-client limit precede forwarding; clean STA-preserving stop");

  route_fake::createFail=true;assert(start(request()));drive(Phase::Stopped);route_fake::createFail=false;
  assert(route_fake::buffers.empty()&&route_fake::sockets.empty()&&route_fake::mode==WIFI_MODE_STA);
  route_fake::configMismatch=true;assert(start(request()));drive(Phase::Stopped);route_fake::configMismatch=false;
  assert(route_fake::buffers.empty()&&route_fake::sockets.empty()&&!route_fake::napt);
  pass("partial AP start/private-config mismatch clean up before any forwarding");

  route_fake::failSocketAt=route_fake::socketCalls+1;
  assert(start(request()));drive(Phase::Stopped);route_fake::failSocketAt=-1;
  assert(route_fake::sockets.empty()&&route_fake::buffers.empty());
  route_fake::naptEnableFail=true;assert(start(request()));drive(Phase::Stopped);route_fake::naptEnableFail=false;
  assert(!route_fake::napt&&route_fake::sockets.empty());
  pass("partial socket allocation and partially successful NAPT enable are cleaned through checked stop");

  started();
  const auto owner=route_fake::current;route_fake::current=reinterpret_cast<void*>(2);
  assert(status().error==Error::Owner&&!stop()&&!start(request()));poll(route_fake::now);
  route_fake::current=owner;assert(status().phase==Phase::Active);stopped();
  pass("foreign owner cannot inspect/control the live route");

  started();const auto ap=apFd(),up=upFd();const auto peer=status().ap_ip+1;
  auto q=query(0x4321);submit(ap,peer,12345,q);submit(ap,peer,12345,q);tick();
  auto& requests=route_fake::sockets.at(up).sent;assert(requests.size()==1);
  assert(requests[0].ip==route_fake::dns&&requests[0].port==53&&requests[0].bytes[0]==0x12&&requests[0].bytes[1]==0x34);
  auto answer=requests[0].bytes;answer[2]|=0x80;
  submit(up,route_fake::dns+1,53,answer);
  auto wrong=answer;wrong[1]^=1;submit(up,route_fake::dns,53,wrong);
  wrong=answer;wrong[13]='y';submit(up,route_fake::dns,53,wrong);
  tick();assert(route_fake::sockets.at(ap).sent.empty());
  submit(up,route_fake::dns,53,answer);tick();
  const auto& replies=route_fake::sockets.at(ap).sent;assert(replies.size()==1);
  assert(replies[0].ip==peer&&replies[0].port==12345&&replies[0].bytes[0]==0x43&&replies[0].bytes[1]==0x21);
  assert(status().queries==1&&status().replies==1);stopped();
  pass("actual DNS relay matches exact upstream/nonce/question, drops duplicates, restores client ID");

  started();const auto ap2=apFd(),up2=upFd();
  for(unsigned i=0;i<5;++i)submit(ap2,status().ap_ip+1,12345,query(uint16_t(i+1)));
  tick();tick();assert(route_fake::sockets.at(up2).sent.size()==4);
  std::set<uint16_t> ids;
  for(const auto& d:route_fake::sockets.at(up2).sent)ids.insert(uint16_t(d.bytes[0])<<8|d.bytes[1]);assert(ids.size()==4);
  tick(3001);submit(ap2,status().ap_ip+1,12345,query(99));tick();assert(route_fake::sockets.at(up2).sent.size()==5);
  stopped();pass("four-request limit, nonce collisions and three-second expiry bound DNS work");

  started();const auto fuzzFd=apFd();std::mt19937 random(9304);
  for(unsigned i=0;i<5000;++i){
    const auto n=random()%1300;std::vector<uint8_t> bytes(n);for(auto& b:bytes)b=uint8_t(random());
    submit(fuzzFd,status().ap_ip+1,12345,std::move(bytes));tick(1);
  }
  assert(status().phase==Phase::Active);stopped();pass("5000 bounded hostile DNS datagrams through actual adapter with sanitizers");

  started();route_fake::bssid[0]^=1;tick();drive(Phase::Stopped);
  assert(status().error==Error::UplinkChanged&&!status().station_preserved);route_fake::bssid[0]^=1;
  pass("same-address upstream BSSID change cancels routing without touching STA");

  route_fake::now=UINT32_MAX-50;auto shortWindow=request();shortWindow.window_ms=1000;
  assert(start(shortWindow));drive(Phase::Active);tick(1001);drive(Phase::Stopped);
  assert(status().error==Error::Expired&&status().remaining_ms==0);
  pass("finite assistance window expires across clock wrap and performs checked cleanup");

  for(unsigned i=0;i<20;++i){started();stopped();}
  assert(route_fake::allocations==route_fake::frees);
  assert(route_fake::buffers.empty()&&route_fake::sockets.empty()&&route_fake::handlers.empty());
  pass("20 repeated starts/stops release all DNS buffers/sockets/handlers; STA remains connected");
  std::cout<<groups<<" actual-adapter boundary groups passed; no hardware/cloud/NVS/filesystem/network access\n";
}
