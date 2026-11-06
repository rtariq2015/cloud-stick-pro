#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "probe_state.h"
#include <algorithm>
#include <cassert>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
class HTTPClient{
  WiFiClientSecure* tls_=nullptr;probe_test::Sent request_;probe_test::Reply reply_;
  bool authorized_=false,ended_=false;
 public:
  HTTPClient(){++probe_test::state.liveHTTP;}
  ~HTTPClient(){end();--probe_test::state.liveHTTP;}
  void setConnectTimeout(unsigned ms){assert(ms==10000);}
  void setTimeout(unsigned ms){assert(ms==15000);}
  void setReuse(bool value){assert(!value);}
  void setFollowRedirects(int mode){assert(mode==HTTPC_DISABLE_FOLLOW_REDIRECTS);}
  bool begin(WiFiClientSecure& tls,const String& endpoint){
    auto& state=probe_test::state;tls_=&tls;request_.endpoint=endpoint;
    assert(endpoint.startsWith("https://"));++state.begins;
    if(state.afterBegin)state.afterBegin();return state.beginOk;
  }
  void setAuthorization(const char* user,const char* password){
    assert(user&&std::string(user)=="synthetic-probe-user");
    assert(password&&std::string(password)=="synthetic-probe-password");authorized_=true;
  }
  void addHeader(const char* key,const String& value){request_.headers[key]=value;}
  int sendRequest(const char* method,const char* body){return send(method,body?body:"");}
  int sendRequest(const char* method,uint8_t*,size_t){return send(method,"");}
  int sendRequest(const char* method,Stream*,size_t){return send(method,"");}
  int GET(){return send("GET","");}
  int getSize()const{return reply_.declared==-2?int(reply_.body.size()):reply_.declared;}
  int writeToStream(Stream* sink){
    size_t done=0;
    while(done<reply_.body.size()){
      if(probe_test::state.duringRead)probe_test::state.duringRead(done);
      const auto bytes=std::min(size_t(32),reply_.body.size()-done);
      if(sink->write(reinterpret_cast<const uint8_t*>(reply_.body.data()+done),bytes)!=bytes)return -1;
      done+=bytes;if(reply_.readFail)return -1;
    }
    return int(done);
  }
  void end(){if(!ended_){ended_=true;++probe_test::state.ends;}}
 private:
  int send(const char* method,const std::string& body){
    auto& state=probe_test::state;assert(authorized_&&tls_);++state.attempts;
    // A failed trust/hostname handshake cannot deliver authorization or an
    // application request to the synthetic destination.
    if(!tls_->trusted||!state.tlsOk||!state.hostnameOk){++state.deniedTLS;return -1;}
    assert(std::string(method)=="PROPFIND"&&!state.replies.empty());
    request_.method=method;request_.body=body;state.received.push_back(request_);
    reply_=std::move(state.replies.front());state.replies.pop_front();
    if(state.afterRequest)state.afterRequest();return reply_.status;
  }
};
