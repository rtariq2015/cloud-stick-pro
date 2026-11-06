#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace probe_test {
struct Reply{int status=207;std::string body;int declared=-2;bool readFail=false;};
struct Sent{std::string method,endpoint,body;std::map<std::string,std::string> headers;};
struct State{
  unsigned liveTLS=0,liveHTTP=0,begins=0,ends=0,attempts=0,deniedTLS=0;
  unsigned storageReads=0,fileOpens=0,transferEvents=0,maintenanceReads=0;
  int wifi=3;int64_t utc=1793793600;
  bool beginOk=true,tlsOk=true,hostnameOk=true,cancelled=false,failBuffer=false;
  std::deque<Reply> replies;std::vector<Sent> received;
  std::function<void()> afterBegin,afterRequest;
  std::function<void(size_t)> duringRead;
};
inline State state;
}
