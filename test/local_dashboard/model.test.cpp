#include "local_dashboard.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
using namespace pro::local_dashboard;
static unsigned cases=0;
static void request(const std::string& value,Route expected){++cases;assert(route(value.data(),value.size(),"192.0.2.9")==expected);}
int main(){
  request("GET / HTTP/1.1\r\nHost: 192.0.2.9\r\n\r\n",Route::Page);
  request("GET /app.js HTTP/1.1\r\nHost: 192.0.2.9:80\r\n\r\n",Route::Script);
  request("GET /status HTTP/1.1\r\nhOsT: 192.0.2.9\r\nContent-Length: 0\r\nOrigin: http://192.0.2.9\r\nSec-Fetch-Site: same-origin\r\n\r\n",Route::Status);
  for(const auto* path:{"/config","/history","/owner-card.json","/diagnostics","/wifi-scan","/resume","/status?secret=bad","//status","/%73tatus","http://192.0.2.9/status"})
    request(std::string("GET ")+path+" HTTP/1.1\r\nHost: 192.0.2.9\r\n\r\n",Route::BadRequest);
  for(const auto* first:{"POST /status HTTP/1.1","HEAD / HTTP/1.1","GET / HTTP/1.0","GET /  HTTP/1.1"})
    request(std::string(first)+"\r\nHost: 192.0.2.9\r\n\r\n",Route::BadRequest);
  for(const auto* headers:{"Host: 192.0.2.9\r\nHost: 192.0.2.9\r\n","Host: 192.0.2.9\r\nContent-Length: 1\r\n",
      "Host: 192.0.2.9\r\nContent-Length: 0\r\nContent-Length: 0\r\n","Host: 192.0.2.9\r\nTransfer-Encoding: chunked\r\n",
      "Host: 192.0.2.9\r\nUpgrade: websocket\r\n","Host: 192.0.2.9\r\n x-header: folded\r\n",
      "Host: 192.0.2.9\r\nOrigin: http://192.0.2.9\r\nOrigin: http://192.0.2.9\r\n","Host: 192.0.2.9\r\nSec-Fetch-Site: none\r\nSec-Fetch-Site: none\r\n"})
    request(std::string("GET /status HTTP/1.1\r\n")+headers+"\r\n",Route::BadRequest);
  for(const auto* host:{"evil.example","192.0.2.9.evil.example","192.0.2.9:81","192.0.2.90"})
    request(std::string("GET /status HTTP/1.1\r\nHost: ")+host+"\r\n\r\n",Route::Forbidden);
  request("GET /status HTTP/1.1\r\nHost: 192.0.2.9\r\nOrigin: https://evil.example\r\n\r\n",Route::Forbidden);
  request("GET /status HTTP/1.1\r\nHost: 192.0.2.9\r\nSec-Fetch-Site: cross-site\r\n\r\n",Route::Forbidden);
  request("GET /status HTTP/1.1\r\nHost: 192.0.2.9\r\nSec-Fetch-Site: same-site\r\n\r\n",Route::Forbidden);
  request("GET / HTTP/1.1\r\nHost: 192.0.2.9\r\nSec-Fetch-Site: cross-site\r\n\r\n",Route::Page);
  request("GET /status HTTP/1.1\r\n\r\n",Route::BadRequest);
  request(std::string(HeaderLimit+1,'a'),Route::BadRequest);
  std::string nul="GET /status HTTP/1.1\r\nHost: 192.0.2.9\r\nX: ";nul.push_back(0);nul+="\r\n\r\n";request(nul,Route::BadRequest);
  for(size_t size=0;size<40;++size)request(std::string("GET /status HTTP/1.1\r\nHost: 192.0.2.9\r\n\r\n").substr(0,size),Route::BadRequest);
  Snapshot s;s.firmware="0.4.0-dashboard1";s.wifi=true;s.media=true;s.verified=4;s.has_scan=true;s.done=UINT64_MAX;s.error=-2303;s.uptime=UINT32_MAX;
  char output[StatusLimit]{};const auto size=json(s,output,sizeof output);assert(size&&size<sizeof output);
  assert(std::strstr(output,"18446744073709551615")&&std::strstr(output,"\"verified\":4")&&std::strstr(output,"\"error\":-2303"));
  for(size_t limit=0;limit<size+1;++limit){char tiny[StatusLimit]{};assert(json(s,tiny,limit)==0);if(limit)assert(tiny[0]==0);}
  for(const auto* version:{"bad\"version","credential/test","<script>","abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"}){s.firmware=version;assert(json(s,output,sizeof output)==0);}
  assert(!json(s,nullptr,StatusLimit));
  std::mt19937 rng(73191);std::string bytes;
  for(unsigned i=0;i<100000;++i){bytes.resize(rng()%1700);for(char& c:bytes)c=char(rng());(void)route(bytes.data(),bytes.size(),"192.0.2.9");}
  std::printf("PASS %u directed requests, bounded JSON cases, 100000 hostile headers\n",cases);
}
