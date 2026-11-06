#include "pro_ble_lifecycle.h"
#include "pro_ble_message.h"
#include "ble_management.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Gate=pro::BleLifecycle;
using Action=Gate::Action;
using State=Gate::State;
static void physical(Gate& g,uint32_t now){CHECK(g.button(true,now)==Action::None);CHECK(g.button(true,now+3000)==Action::Pause);}
static bool parse(const std::string& s){pro::ble::Request r;return pro::ble::parseRequest(reinterpret_cast<const uint8_t*>(s.data()),s.size(),r);}
int main(){
  // The default header has no runtime BLE dependency or opportunity to initialize it.
  CHECK(CSP_ENABLE_BLE_MANAGEMENT==0);CHECK(!ble_management_begin());CHECK(ble_management_stop());CHECK(!ble_management_active());
  Gate g;
  CHECK(g.poll(500000,false,false,500000,250000)==Action::None);
  CHECK(!g.paused());CHECK(g.button(true,0)==Action::None);CHECK(g.button(true,2999)==Action::None);
  CHECK(g.button(false,3000)==Action::None);CHECK(g.state()==State::Idle);
  physical(g,4000);CHECK(g.paused());
  CHECK(g.poll(8000,true,false,500000,250000)==Action::None); // TLS/worker still owns its resources.
  CHECK(g.button(true,10000)==Action::None); // No reset or second window from a long hold.
  CHECK(g.poll(9000,false,false,500000,250000)==Action::Start);
  CHECK(g.state()==State::Starting);CHECK(!g.stopped(true,true));CHECK(g.started(true));
  CHECK(g.poll(10000,false,true,Gate::ActiveFree,Gate::ActiveLargest)==Action::None);
  CHECK(g.poll(10001,false,true,Gate::ActiveFree-1,Gate::ActiveLargest)==Action::Stop);
  CHECK(g.reason()==Gate::Reason::LowMemory);CHECK(!g.started(true));
  CHECK(g.stopped(true,true));CHECK(!g.paused());
  CHECK(g.button(true,150000)==Action::None); // Holding the same press cannot reopen.
  CHECK(g.button(false,150001)==Action::None);physical(g,150002);
  CHECK(g.poll(198002,true,false,500000,250000)==Action::Stop);
  CHECK(g.reason()==Gate::Reason::QuiesceTimeout);CHECK(g.stopped(true,true));
  CHECK(g.button(false,200000)==Action::None);physical(g,200001);
  CHECK(g.poll(203002,false,false,500000,250000)==Action::Start);CHECK(g.started(false));
  CHECK(g.reason()==Gate::Reason::StartFailed);CHECK(!g.stopped(false,true));CHECK(g.state()==State::Blocked);
  CHECK(g.paused());CHECK(g.button(false,210000)==Action::None);CHECK(g.button(true,220000)==Action::None);
  CHECK(g.button(true,223001)==Action::None); // No USB reset or automatic recovery after failed teardown.
  CHECK(g.poll(1000000,false,false,500000,250000)==Action::None);
  Gate leak;physical(leak,0);CHECK(leak.poll(3001,false,false,500000,250000)==Action::Start);CHECK(leak.started(true));
  CHECK(leak.poll(4000,false,false,500000,250000)==Action::Stop);CHECK(leak.reason()==Gate::Reason::TransportClosed);
  CHECK(!leak.stopped(true,false));CHECK(leak.reason()==Gate::Reason::MemoryNotRestored);CHECK(leak.paused());
  Gate wrap;physical(wrap,std::numeric_limits<uint32_t>::max()-2000);
  CHECK(wrap.poll(1000,false,false,500000,250000)==Action::Start);CHECK(wrap.started(true));
  CHECK(wrap.poll(120998,false,true,500000,250000)==Action::None);
  CHECK(wrap.poll(120999,false,true,500000,250000)==Action::Stop);CHECK(wrap.reason()==Gate::Reason::WindowExpired);
  CHECK(wrap.stopped(true,true));
  CHECK(Gate::canStart(Gate::StartFree,Gate::StartLargest));CHECK(!Gate::canStart(Gate::StartFree-1,Gate::StartLargest));
  CHECK(!Gate::canStart(Gate::StartFree,Gate::StartLargest-1));
  CHECK(Gate::restored(200000,120000,200000-Gate::RestoreTolerance,120000-Gate::RestoreTolerance));
  CHECK(!Gate::restored(200000,120000,200000-Gate::RestoreTolerance-1,120000));
  CHECK(!Gate::restored(200000,120000,200000,120000-Gate::RestoreTolerance-1));
  CHECK(!Gate::restored(10000,10000,10000,10000));
  Gate bench;CHECK(bench.requestClose()==Action::None);
  CHECK(bench.requestWindow(1000)==Action::Pause);
  CHECK(bench.requestWindow(1001)==Action::None);
  CHECK(bench.poll(1002,false,false,500000,250000)==Action::Start);CHECK(bench.started(true));
  CHECK(bench.requestClose()==Action::Stop);CHECK(bench.stopped(true,true));
  CHECK(bench.requestWindow(2000)==Action::Pause);
  CHECK(bench.requestClose()==Action::Stop);CHECK(!bench.stopped(true,false));
  CHECK(bench.requestWindow(3000)==Action::None);CHECK(bench.requestClose()==Action::None);
  for(unsigned i=0;i<100;++i){Gate repeated;physical(repeated,i*10000);CHECK(repeated.poll(i*10000+3001,false,false,500000,250000)==Action::Start);CHECK(repeated.started(true));CHECK(repeated.poll(i*10000+4000,false,false,500000,250000)==Action::Stop);CHECK(repeated.stopped(true,true));}
  CHECK(parse("{\"v\":1,\"id\":1,\"op\":\"info\"}"));CHECK(parse(" { \"v\" : 1, \"id\":2147483647,\"op\":\"status\" } "));
  const char* invalid[]={"","{}","{\"v\":0,\"id\":1,\"op\":\"info\"}","{\"v\":1,\"id\":0,\"op\":\"info\"}","{\"v\":1,\"id\":01,\"op\":\"info\"}","{\"v\":1,\"id\":-1,\"op\":\"info\"}","{\"v\":1,\"id\":2147483648,\"op\":\"info\"}","{\"v\":1,\"id\":1,\"op\":\"reset\"}","{\"v\":1,\"id\":1,\"op\":\"info\",\"password\":\"synthetic\"}","{\"v\":1,\"id\":1,\"op\":\"info\"}junk","{\"v\":1,\"v\":1,\"id\":1,\"op\":\"info\"}","{\"id\":1,\"v\":1,\"op\":\"info\"}"};
  for(const auto* s:invalid)CHECK(!parse(s));
  CHECK(!parse(std::string(97,' ')));pro::ble::Request request;
  CHECK(!pro::ble::parseRequest(nullptr,1,request));
  CHECK(!pro::ble::encryptedFrameSize(0));CHECK(!pro::ble::encryptedFrameSize(15));CHECK(!pro::ble::encryptedFrameSize(16));CHECK(pro::ble::encryptedFrameSize(17));CHECK(pro::ble::encryptedFrameSize(112));CHECK(!pro::ble::encryptedFrameSize(113));CHECK(!pro::ble::encryptedFrameSize(size_t(-1)));
  CHECK(!pro::ble::handshakeFrameSize(0));CHECK(pro::ble::handshakeFrameSize(512));CHECK(!pro::ble::handshakeFrameSize(513));
  pro::ble::RequestSequence seq;CHECK(!seq.accept(1,1));CHECK(seq.open(1));CHECK(!seq.open(2));CHECK(!seq.accept(2,1));CHECK(seq.accept(1,1));CHECK(!seq.accept(1,1));CHECK(!seq.accept(1,0));CHECK(seq.accept(1,2));seq.close();CHECK(!seq.accept(1,3));CHECK(seq.open(2));CHECK(seq.accept(2,1));
  // Hostile bytes exercise the same bounded production parser under sanitizers.
  uint32_t random=0x43535031;unsigned accepted=0;
  for(unsigned i=0;i<100000;++i){uint8_t bytes[128];for(auto& b:bytes){random=random*1664525u+1013904223u;b=uint8_t(random>>24);}size_t n=random%sizeof bytes;if(pro::ble::parseRequest(bytes,n,request))++accepted;}
  CHECK(accepted==0);
  std::printf("%u checks passed; 100000 hostile application messages; 100 lifecycle sessions (desktop only)\n",checks);
}
