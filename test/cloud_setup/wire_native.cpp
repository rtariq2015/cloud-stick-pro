#include <wifi_wire.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using namespace pro::wifi_setup;
bool parses(const std::string& text){WireRequest request;return parseWire(reinterpret_cast<const uint8_t*>(text.data()),text.size(),request);}
int main(){
  unsigned cases=0;
  for(const auto* text:{"{\"v\":2,\"id\":1,\"op\":\"cloud_status\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_begin\",\"revision\":\"1\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_abort\",\"transaction\":\"1\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":0,\"field\":\"username\",\"value\":\"synthetic\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_commit\",\"transaction\":\"1\",\"revision\":\"1\",\"edit\":3,\"accept_unverified\":true}"}){
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
    assert(parses(text));
#else
    assert(!parses(text));
#endif
    ++cases;
  }
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
  for(const auto* text:{"{\"v\":2,\"id\":1,\"op\":\"cloud_begin\",\"revision\":1}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_begin\",\"revision\":\"0\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_begin\",\"revision\":\"01\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_begin\",\"revision\":\"18446744073709551616\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_status\",\"password\":\"must-not-read\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":true,\"field\":\"username\",\"value\":\"synthetic\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":0,\"field\":\"server_url\",\"value\":\"http://untrusted.invalid/\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":0,\"field\":\"ca_pem\",\"value\":\"untrusted\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":0,\"field\":\"username\",\"value\":\"first\",\"value\":\"second\"}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":0,\"field\":\"username\",\"value\":{}}",
    "{\"v\":2,\"id\":1,\"op\":\"cloud_commit\",\"transaction\":\"1\",\"revision\":\"1\",\"edit\":3,\"accept_unverified\":1}"}){assert(!parses(text));++cases;}
  const std::string prefix="{\"v\":2,\"id\":1,\"op\":\"cloud_stage\",\"transaction\":\"1\",\"edit\":0,\"field\":\"app_password\",\"value\":\"";
  assert(parses(prefix+std::string(128,'a')+"\"}"));assert(!parses(prefix+std::string(129,'a')+"\"}"));cases+=2;
  assert(!parses(std::string(497,'a')));++cases;
#endif
  std::printf("PASS %u exact bounded owner-cloud wire cases; disabled targets reject operations\n",cases);
}
