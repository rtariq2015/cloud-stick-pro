#include <wifi_wire.h>
#include "pro_event_log.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace pro::wifi_setup;
static bool parse(const char* text,WireRequest& out){
  return parseWire(reinterpret_cast<const uint8_t*>(text),std::strlen(text),out);
}
int main(){
#if CSP_ENABLE_EVENT_LOG && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  constexpr bool enabled=true;
#else
  constexpr bool enabled=false;
#endif
  WireRequest request;
  for(const char* cursor:{"0","1","4294967295"}){
    const std::string text=std::string("{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":")+cursor+"}";
    const bool accepted=parse(text.c_str(),request);assert(accepted==enabled);
#if CSP_ENABLE_EVENT_LOG && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
    if(accepted){assert(request.op==Op::EventLog);assert(request.cursor==std::stoull(cursor));}
#endif
  }
  const char* bad[]={
    "{\"v\":2,\"id\":1,\"op\":\"event_log\"}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":-1}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":4294967296}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":0.1}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":1e1}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":\"0\"}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":true}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":null}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":{}}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":0,\"owner\":true}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":0,\"cursor\":1}",
    "{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":00}",
    "{\"v\":1,\"id\":1,\"op\":\"event_log\",\"cursor\":0}",
    "{\"v\":2,\"id\":0,\"op\":\"event_log\",\"cursor\":0}",
    "{\"v\":2,\"id\":2147483648,\"op\":\"event_log\",\"cursor\":0}"
  };
  for(const auto* text:bad)assert(!parse(text,request));
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"status\"}",request));
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"list\"}",request));
  std::cout<<"PASS actual event wire: 3 boundaries, 15 negative cases, legacy commands; enabled="<<enabled<<'\n';
}
