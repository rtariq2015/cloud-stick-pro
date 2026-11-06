#include <wifi_wire.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace pro::wifi_setup;
bool parse(const char* value){WireRequest r;return parseWire(reinterpret_cast<const uint8_t*>(value),std::strlen(value),r);}
int main(){
#if defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  constexpr bool enabled=true;
#else
  constexpr bool enabled=false;
#endif
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"portal_begin\",\"profile\":1,\"revision\":\"2\"}")==enabled);
  assert(parse("{\"v\":2,\"id\":2,\"op\":\"portal_handoff\",\"profile\":1,\"revision\":\"2\",\"grant\":\"1\"}")==enabled);
  assert(parse("{\"v\":2,\"id\":3,\"op\":\"portal_status\"}")==enabled);
  assert(parse("{\"v\":2,\"id\":4,\"op\":\"portal_cancel\",\"grant\":\"1\"}")==enabled);
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_begin\",\"profile\":0,\"revision\":\"2\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_begin\",\"profile\":1,\"revision\":2}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_begin\",\"profile\":1,\"revision\":\"02\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_handoff\",\"profile\":1,\"revision\":\"2\",\"grant\":\"0\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_handoff\",\"profile\":1,\"revision\":\"2\",\"grant\":\"18446744073709551616\"}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_status\",\"owner\":true}"));
  assert(!parse("{\"v\":2,\"id\":1,\"op\":\"portal_status\",\"op\":\"status\"}"));
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"status\"}"));
  std::cout<<"PASS actual public-WiFi wire boundaries; enabled="<<enabled<<'\n';
}
