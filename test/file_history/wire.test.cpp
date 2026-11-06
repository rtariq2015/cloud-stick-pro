#include <wifi_wire.h>
#include <cassert>
#include <iostream>
#include <string>
using namespace pro::wifi_setup;
static bool parse(const std::string& text,WireRequest& out){return parseWire(reinterpret_cast<const uint8_t*>(text.data()),text.size(),out);}
int main(){
#if defined(CSP_ENABLE_FILE_HISTORY) && CSP_ENABLE_FILE_HISTORY && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
 constexpr bool enabled=true;
#else
 constexpr bool enabled=false;
#endif
 WireRequest r;assert(parse("{\"v\":2,\"id\":1,\"op\":\"history_status\"}",r)==enabled);
 for(const auto g:{"1","4294967295"})for(const auto i:{"0","7"})for(const auto o:{"0","64","240"}){
  const std::string text=std::string("{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":")+g+",\"i\":"+i+",\"o\":"+o+"}";
  assert(parse(text,r)==enabled);
#if defined(CSP_ENABLE_FILE_HISTORY) && CSP_ENABLE_FILE_HISTORY && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  assert(r.op==Op::HistoryRow&&r.edit==std::stoul(g)&&r.profile==std::stoul(i)&&r.cursor==std::stoul(o));
#endif
 }
 for(const char* bad:{"-1","0.1","1e1","\"1\"","true","null","{}","4294967296","00"}){
  assert(!parse(std::string("{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":")+bad+",\"i\":0,\"o\":0}",r));
  assert(!parse(std::string("{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"i\":")+bad+",\"o\":0}",r));
  assert(!parse(std::string("{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"i\":0,\"o\":")+bad+"}",r));
 }
 for(const auto text:{
  "{\"v\":2,\"id\":1,\"op\":\"history_status\",\"g\":1}",
  "{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":0,\"i\":0,\"o\":0}",
  "{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"i\":8,\"o\":0}",
  "{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"i\":0,\"o\":241}",
  "{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"i\":0}",
  "{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"g\":2,\"i\":0,\"o\":0}",
  "{\"v\":2,\"id\":1,\"op\":\"history_row\",\"g\":1,\"i\":0,\"o\":0,\"path\":\"private\"}",
  "{\"v\":1,\"id\":1,\"op\":\"history_status\"}",
  "{\"v\":2,\"id\":0,\"op\":\"history_status\"}"
 })assert(!parse(text,r));
 assert(parse("{\"v\":2,\"id\":1,\"op\":\"status\"}",r));
 std::cout<<"PASS actual history wire boundaries and malformed messages; enabled="<<enabled<<'\n';
}
