#include "local_dashboard.h"
#include <cstdio>
#include <cstring>
#include <string_view>
namespace pro::local_dashboard {
namespace {
bool equal(std::string_view a,const char* b){return a==b;}
bool iequal(std::string_view a,const char* b){
  const size_t n=std::strlen(b);if(a.size()!=n)return false;
  for(size_t i=0;i<n;++i){char c=a[i];if(c>='A'&&c<='Z')c+=32;if(c!=b[i])return false;}
  return true;
}
}
Route route(const char* raw,size_t n,const char* host){
  if(!raw||!host||n<4||n>HeaderLimit)return Route::BadRequest;
  std::string_view input(raw,n);
  if(input.substr(n-4)!="\r\n\r\n")return Route::BadRequest;
  for(unsigned char c:input)if(c==0||c>=127||(c<32&&c!='\r'&&c!='\n'&&c!='\t'))return Route::BadRequest;
  auto end=input.find("\r\n");if(end==std::string_view::npos)return Route::BadRequest;
  const auto first=input.substr(0,end);Route target=Route::BadRequest;
  if(first=="GET / HTTP/1.1")target=Route::Page;
  else if(first=="GET /app.js HTTP/1.1")target=Route::Script;
  else if(first=="GET /status HTTP/1.1")target=Route::Status;
  if(target==Route::BadRequest)return target;
  bool saw_host=false,saw_length=false,saw_origin=false,saw_site=false,cross_site=false;
  size_t pos=end+2;
  while(pos<n-2){
    end=input.find("\r\n",pos);if(end==std::string_view::npos)return Route::BadRequest;
    auto line=input.substr(pos,end-pos);const auto colon=line.find(':');
    if(colon==std::string_view::npos||colon==0)return Route::BadRequest;
    auto key=line.substr(0,colon),value=line.substr(colon+1);
    for(char c:key)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return Route::BadRequest;
    while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.remove_prefix(1);
    while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.remove_suffix(1);
    for(unsigned char c:value)if(c<32||c>=127)return Route::BadRequest;
    if(iequal(key,"host")){
      if(saw_host)return Route::BadRequest;
      saw_host=true;
      const size_t size=std::strlen(host);
      if(!equal(value,host)&&!(value.size()==size+3&&value.substr(0,size)==host&&value.substr(size)==":80"))return Route::Forbidden;
    }else if(iequal(key,"content-length")){
      if(saw_length||value!="0")return Route::BadRequest;
      saw_length=true;
    }else if(iequal(key,"transfer-encoding")||iequal(key,"upgrade"))return Route::BadRequest;
    else if(iequal(key,"origin")){
      if(saw_origin)return Route::BadRequest;
      saw_origin=true;
      char expected[40]{};const int count=std::snprintf(expected,sizeof expected,"http://%s",host);
      if(count<=0||size_t(count)>=sizeof expected||value!=expected)return Route::Forbidden;
    }else if(iequal(key,"sec-fetch-site")){
      if(saw_site)return Route::BadRequest;
      saw_site=true;
      cross_site=value=="cross-site"||value=="same-site";
      if(value!="none"&&value!="same-origin"&&!cross_site)return Route::BadRequest;
    }
    pos=end+2;
  }
  if(!saw_host)return Route::BadRequest;
  // A navigation from an external link may display the public shell, but
  // scripts/status cannot be fetched by an external origin. No CORS headers.
  if(cross_site&&target!=Route::Page)return Route::Forbidden;
  return target;
}
size_t json(const Snapshot& s,char* out,size_t size){
  if(!out||!size)return 0;
  // Firmware is compile-time metadata, still restrict it before JSON emission.
  const char* version=s.firmware?s.firmware:"";
  size_t length=0;for(;version[length];++length){
    char c=version[length];if(length>=48||!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-')){out[0]=0;return 0;}
  }
  const int n=std::snprintf(out,size,
    "{\"firmware\":\"%s\",\"uptime_ms\":%lu,\"wifi\":%s,\"media\":%s,\"active\":%s,\"scanning\":%s,"
    "\"has_scan\":%s,\"scan_complete\":%s,\"window_open\":%s,\"paused\":%s,\"cloud\":%s,\"configured\":%s,"
    "\"verified\":%lu,\"pending\":%lu,\"failed\":%lu,\"done\":%llu,\"total\":%llu,\"bytes_per_second\":%lu,"
    "\"quiet_seconds\":%lu,\"last_backup_unix\":%llu,\"error\":%d,\"percent\":%d,\"phase\":%u}",
    version,(unsigned long)s.uptime,s.wifi?"true":"false",s.media?"true":"false",s.active?"true":"false",s.scanning?"true":"false",
    s.has_scan?"true":"false",s.scan_complete?"true":"false",s.window_open?"true":"false",s.paused?"true":"false",s.cloud?"true":"false",s.configured?"true":"false",
    (unsigned long)s.verified,(unsigned long)s.pending,(unsigned long)s.failed,(unsigned long long)s.done,(unsigned long long)s.total,
    (unsigned long)s.speed,(unsigned long)s.quiet,(unsigned long long)s.last_backup,s.error,s.percent,unsigned(s.phase));
  if(n<0||size_t(n)>=size){out[0]=0;return 0;}return size_t(n);
}
}
