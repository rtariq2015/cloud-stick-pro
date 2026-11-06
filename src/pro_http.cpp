#include "pro_http.h"
#include <algorithm>
#include <cctype>
namespace pro {
bool dashboardRoute(const std::string& method,const std::string& path){
  return (method=="GET"&&(path=="/"||path=="/app.js"||path=="/status"||path=="/diagnostics"||path=="/favicon.ico"))||(method=="POST"&&path=="/history");
}

bool constantEqual(const std::string& a,const std::string& b) {
  if(a.size()!=b.size())return false;unsigned diff=0;for(size_t i=0;i<a.size();++i)diff|=unsigned(a[i]^b[i]);return diff==0;
}
bool parseRequest(const std::string& header,Request& request,const char** reason,const std::string& expectedHost) {
  if(reason)*reason=nullptr;
  const auto fail=[reason](const char* message){if(reason)*reason=message;return false;};
  if(header.size()>HeaderLimit||(header.size()==HeaderLimit&&header.compare(header.size()-4,4,"\r\n\r\n")!=0))return fail("Request headers exceed the 2048-byte limit.");
  if(header.size()<4 || header.substr(header.size()-4)!="\r\n\r\n")return fail("Incomplete request headers. Please retry.");
  size_t end=header.find("\r\n");if(end==std::string::npos||end>100)return fail("Invalid HTTP request line.");
  std::string first=header.substr(0,end);size_t a=first.find(' '),b=first.find(' ',a==std::string::npos?0:a+1);
  if(a==std::string::npos||b==std::string::npos||first.substr(b+1)!="HTTP/1.1")return fail("This setup page requires HTTP/1.1.");
  Request r;r.method=first.substr(0,a);r.path=first.substr(a+1,b-a-1);
  if(r.method!="GET"&&r.method!="POST")return fail("Only GET and POST requests are supported.");
  if(r.path!="/"&&r.path!="/history"&&r.path!="/status"&&r.path!="/app.js"&&r.path!="/config"&&r.path!="/resume"&&r.path!="/wifi-scan"&&r.path!="/wifi-connect"&&r.path!="/wifi-disconnect"&&r.path!="/presets"&&r.path!="/favicon.ico"&&r.path!="/diagnostics"&&r.path!="/cloud-check"&&r.path!="/cloud-test")return fail("Unknown setup page. Open the setup address shown on the dongle.");
  bool host=false,length=false,type=false,auth=false,csrf=false;
  size_t start=end+2;
  while(start<header.size()-2) {
    end=header.find("\r\n",start);if(end==std::string::npos)return fail("Incomplete request headers. Please retry.");
    std::string line=header.substr(start,end-start);size_t colon=line.find(':');if(colon==std::string::npos||colon==0)return fail("Malformed HTTP header.");
    std::string key=line.substr(0,colon),value=line.substr(colon+1);
    for(char& c:key){if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-')return fail("Invalid HTTP header name.");c=std::tolower(static_cast<unsigned char>(c));}
    while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(0,1);
    while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.pop_back();
    for(unsigned char c:value)if(c<32||c>=127)return fail("Invalid HTTP header value.");
    if(key=="host") {
      if(host)return fail("Duplicate Host header.");
      if(value!=expectedHost&&value!=expectedHost+":80")return fail("Use the setup address shown on the dongle while connected to its Wi-Fi.");
      host=true;
    } else if(key=="content-length") {
      if(length)return fail("Duplicate Content-Length header.");
      if(value.empty()||value.size()>4)return fail("Invalid Content-Length header.");
      length=true;for(char c:value){if(c<'0'||c>'9')return fail("Invalid Content-Length header.");r.length=r.length*10+c-'0';}
      if(r.length>BodyLimit)return fail("Request body exceeds the 6144-byte limit.");
    } else if(key=="transfer-encoding")return fail("Chunked request bodies are not supported.");
    else if(key=="content-type") {
      if(type)return fail("Duplicate Content-Type header.");
      if(value!="application/json")return fail("Expected Content-Type: application/json.");
      type=true;
    } else if(key=="authorization") {
      if(auth)return fail("Duplicate Authorization header.");
      if(value.size()>128)return fail("Authorization header exceeds its size limit.");
      auth=true;r.authorization=value;
    } else if(key=="x-csrf-token") {
      if(csrf)return fail("Duplicate request token header.");
      if(value.size()>64)return fail("Request token header exceeds its size limit.");
      csrf=true;r.csrf=value;
    }
    start=end+2;
  }
  if(!host)return fail("Missing Host header. Open the setup address shown on the dongle.");
  if(r.method=="GET"&&r.length)return fail("GET requests must not contain a body.");
  if(r.method=="POST") {
    if(!length)return fail("POST requests require Content-Length.");
    if(!type)return fail("POST requests require Content-Type: application/json.");
    if(r.length<2)return fail("POST requests require a JSON body.");
  }
  request=r;return true;
}
}
