#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <vector>

constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
struct FakeReply {std::string method;int status;std::string body;};
extern std::deque<FakeReply> fakeReplies;
extern std::vector<std::string> sentMethods;
extern std::function<void(unsigned)> afterBegin,afterRequest;
extern std::function<void(unsigned,size_t)> duringRead;
extern std::function<void()> duringUpload;
extern unsigned beginCount,liveHTTP;

class HTTPClient {
  FakeReply reply;
  bool authorized=false;
 public:
  HTTPClient(){++liveHTTP;}
  ~HTTPClient(){--liveHTTP;}
  void setConnectTimeout(unsigned timeout){assert(timeout==10000);}
  void setTimeout(unsigned timeout){assert(timeout==15000);}
  void setReuse(bool reuse){assert(!reuse);}
  void setFollowRedirects(int mode){assert(mode==HTTPC_DISABLE_FOLLOW_REDIRECTS);}
  bool begin(WiFiClientSecure& tls,const String& endpoint){
    assert(tls.trusted&&endpoint.startsWith("https://"));
    ++beginCount;if(afterBegin)afterBegin(beginCount);return true;
  }
  void setAuthorization(const char* user,const char* password){assert(user&&*user&&password&&*password);authorized=true;}
  void addHeader(const char*,const String&){}
  int sendRequest(const char* method,const char*){return send(method);}
  int sendRequest(const char* method,uint8_t*,size_t){return send(method);}
  int sendRequest(const char* method,Stream* source,size_t size){
    int code=send(method);size_t done=0;uint8_t bytes[16];
    while(done<size&&source->available()>0){
      if(duringUpload)duringUpload();
      auto read=source->readBytes(bytes,std::min(sizeof bytes,size-done));
      if(!read)break;done+=read;
    }
    return code;
  }
  int GET(){return send("GET");}
  int getSize()const{return int(reply.body.size());}
  int writeToStream(Stream* stream){
    size_t done=0;
    while(done<reply.body.size()){
      if(duringRead)duringRead(unsigned(sentMethods.size()),done);
      size_t n=std::min(size_t(16),reply.body.size()-done);
      if(stream->write(reinterpret_cast<const uint8_t*>(reply.body.data()+done),n)!=n)return -1;
      done+=n;
    }
    return int(done);
  }
  void end(){}
 private:
  int send(const char* method){
    assert(authorized&&!fakeReplies.empty());
    reply=std::move(fakeReplies.front());fakeReplies.pop_front();
    assert(reply.method==method);sentMethods.emplace_back(method);
    if(afterRequest)afterRequest(unsigned(sentMethods.size()));
    return reply.status;
  }
};
