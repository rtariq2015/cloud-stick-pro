#include "wifi_wire.h"
#include "owner_record.h"
#include "owner_group.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
using namespace pro::wifi_setup;
size_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::abort();}}while(0)
bool parse(const std::string& s){WireRequest r;return parseWire(reinterpret_cast<const uint8_t*>(s.data()),s.size(),r);}
int main(int argc,char** argv){
  CHECK(argc==2);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
  uint8_t mac[6]={0,0x11,0x22,0x33,0x44,0x55};OwnerRecord owner;
  CHECK(decodeOwner(bytes.data(),bytes.size(),mac,owner));
  for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;CHECK(!decodeOwner(bad.data(),bad.size(),mac,owner));}
  ++mac[5];CHECK(!decodeOwner(bytes.data(),bytes.size(),mac,owner));--mac[5];
  CHECK(!decodeOwner(bytes.data(),bytes.size()-1,mac,owner));
  for(const std::string good:{R"({"v":2,"id":1,"op":"list"})",R"({"v":2,"id":2,"op":"scan"})",
    R"({"v":2,"id":3,"op":"scan_page","cursor":0})",R"({"v":2,"id":4,"op":"begin","revision":"1"})",
    R"({"v":2,"id":5,"op":"stage","transaction":"1","profile":0,"ssid":"CSP Test","password":"synthetic-123","open":false,"keep":false,"priority":0,"enabled":true})",
    R"({"v":2,"id":6,"op":"check","transaction":"1","profile":1})",
    R"({"v":2,"id":7,"op":"commit","transaction":"1","revision":"1","edit":1,"accept_local":true})",
    R"({"v":2,"id":8,"op":"result","job":1})"})CHECK(parse(good));
  for(const std::string bad:{R"({"v":2,"id":1,"op":"list","id":2})",R"({"v":2,"id":1,"op":"list","owner":true})",
    R"({"v":2,"id":1,"op":"scan\u0000"})",R"({"v":2,"id":1,"op":"list",})",
    R"({"v":2,"id":01,"op":"list"})",R"({"v":2,"id":-1,"op":"list"})",
    R"({"v":2,"id":1.0,"op":"list"})",R"({"v":2,"id":1e0,"op":"list"})",
    R"({"v":2,"id":2147483648,"op":"list"})",R"({"v":1,"id":1,"op":"list"})",
    R"({"v":2,"id":1,"op":"list","nested":{}})",R"({"v":2,"id":1,"op":"list","data":[]})",
    R"({"v":2,"id":1,"op":"begin","revision":1})",R"({"v":2,"id":1,"op":"begin","revision":"01"})",
    R"({"v":2,"id":1,"op":"begin","revision":"18446744073709551616"})",
    R"({"v":2,"id":1,"op":"begin","revision":"0"})",R"({"v":2,"id":1,"op":"scan_page","cursor":21})",
    R"({"v":2,"id":1,"op":"commit","transaction":"1","revision":"1","edit":1,"accept_local":true,"workers_quiescent":true})"})CHECK(!parse(bad));
  const std::string boundary=R"({"v":2,"id":1,"op":"status"})";
  CHECK(parse(boundary+std::string(WifiWireLimit-boundary.size(),' ')));
  CHECK(!parse(boundary+std::string(WifiWireLimit+1-boundary.size(),' ')));
  std::mt19937 random(0x43575032);
  for(unsigned i=0;i<100000;++i){std::string bad(random()%600,' ');for(auto& c:bad)c=char(random());CHECK(!parse(bad));}
  // Sanitizer coverage includes the actual bounded parser allocator and its
  // cleanup on every malformed-message path; no stderr is allowed by the runner.
  std::cout<<"PASS "<<checks<<" owner/wire checks; 100000 hostile frames\n";
}
