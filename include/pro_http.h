#pragma once
#include <string>
#include <cstddef>
namespace pro {
bool dashboardRoute(const std::string& method,const std::string& path);
constexpr size_t HeaderLimit=2048,BodyLimit=6144;
struct Request {std::string method,path,authorization,csrf;size_t length=0;};
// Restricted maintenance HTTP dialect: fixed AP Host, JSON Content-Length POST,
// no chunked/multipart bodies, no duplicate security/framing headers.
// Optional reason points only to static text; never reflects request values.
bool parseRequest(const std::string& header,Request& request,const char** reason=nullptr,const std::string& expectedHost="192.168.4.1");
bool constantEqual(const std::string& a,const std::string& b);
}
