#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <vector>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0,HTTP_TCP_TX_BUFFER_SIZE=1460;
constexpr int HTTPC_ERROR_CONNECTION_REFUSED=-1,HTTPC_ERROR_SEND_HEADER_FAILED=-2,HTTPC_ERROR_SEND_PAYLOAD_FAILED=-3;
constexpr int HTTPC_ERROR_NO_STREAM=-6,HTTPC_ERROR_TOO_LESS_RAM=-8;
struct FakeReply {std::string method;int status;std::string body;};
extern std::deque<FakeReply> fakeReplies;
extern std::vector<std::string> sentMethods;
extern unsigned liveHTTP;
extern std::function<void()> beforePayload;
class HTTPClient {
  FakeReply reply;bool authorized=false;WiFiClientSecure* _client=nullptr;
 public:
  HTTPClient(){++liveHTTP;}~HTTPClient(){--liveHTTP;}
  void setConnectTimeout(unsigned n){assert(n==10000);}void setTimeout(unsigned n){assert(n==15000);}
  void setReuse(bool value){assert(!value);}void setFollowRedirects(int n){assert(n==HTTPC_DISABLE_FOLLOW_REDIRECTS);}
  bool begin(WiFiClientSecure& client,const String& endpoint){assert(client.trusted&&endpoint.startsWith("https://"));_client=&client;return true;}
  void setAuthorization(const char* user,const char* secret){assert(user&&*user&&secret&&*secret);authorized=true;}
  void addHeader(const char*,const String&){}
  int sendRequest(const char* method,const char*){return send(method);}
  int sendRequest(const char* method,uint8_t*,size_t){return send(method);}
  int sendRequest(const char*,Stream*,size_t);
  int GET(){return send("GET");}
  int getSize()const{return int(reply.body.size());}
  int writeToStream(Stream* stream){
    size_t done=0;while(done<reply.body.size()){
      const size_t n=std::min(size_t(16),reply.body.size()-done);
      if(stream->write(reinterpret_cast<const uint8_t*>(reply.body.data()+done),n)!=n)return -1;done+=n;
    }return int(done);
  }
  void end(){}
 private:
  bool connect(){return _client&&_client->connected();}
  bool connected(){return _client&&(_client->available()>0||_client->connected());}
  bool generateCookieString(String*){return false;}
  bool sendHeader(const char* method){
    send(method);const uint8_t header[]={0x48,0x54,0x54,0x50};
    const bool result=_client->write(header,sizeof header)==sizeof header;
    if(beforePayload)beforePayload();return result;
  }
  int returnError(int code){return code;}
  int handleHeaderResponse(){return reply.status;}
  int send(const char* method){
    assert(authorized&&!fakeReplies.empty());reply=std::move(fakeReplies.front());fakeReplies.pop_front();
    assert(reply.method==method);sentMethods.emplace_back(method);return reply.status;
  }
};
