// Compile the actual wire parser in default, explicitly disabled and enabled
// modes. The new read-only opcode never leaks into the legacy setup protocol.
#include <wifi_wire.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace pro::wifi_setup;
bool parse(const char* text){WireRequest request;return parseWire(reinterpret_cast<const uint8_t*>(text),std::strlen(text),request);}
int main(){
#if defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  constexpr bool enabled=true;
#else
  constexpr bool enabled=false;
#endif
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"backup_status\"}")==enabled);
  assert(parse("{\"v\":2,\"id\":2147483647,\"op\":\"backup_status\"}")==enabled);
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"status\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"backup_statusx\"}"));
  assert(!parse("{\"v\":2,\"id\":0,\"op\":\"backup_status\"}"));
  assert(!parse("{\"v\":2,\"id\":2147483648,\"op\":\"backup_status\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"backup_status\",\"extra\":true}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"backup_status\",\"op\":\"status\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"backup_status\",\"x\":{}}"));
  std::cout<<"PASS actual backup_status wire opcode; enabled="<<enabled<<"\n";
}
