#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <cassert>
#include <deque>
#include <vector>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0,HTTP_TCP_RX_BUFFER_SIZE=1460;
constexpr int HTTPC_TE_IDENTITY=0,HTTPC_TE_CHUNKED=1;
constexpr int HTTPC_ERROR_NO_STREAM=-6,HTTPC_ERROR_TOO_LESS_RAM=-8,HTTPC_ERROR_STREAM_WRITE=-10;
constexpr int HTTPC_ERROR_NOT_CONNECTED=-4,HTTPC_ERROR_CONNECTION_LOST=-5,HTTPC_ERROR_READ_TIMEOUT=-11,HTTPC_ERROR_ENCODING=-9;
struct BodyReply {
 std::string method;int status=200;std::string bytes;int declared=-2;bool chunked=false,stayOpen=false;
 ReadMode mode=ReadMode::Normal;
};
extern std::deque<BodyReply> bodyReplies;
extern std::vector<std::string> bodyMethods;
extern unsigned liveHTTP;
extern std::function<void()> bodyPoll;
class HTTPClient {
 NetworkClient* _client=nullptr;int _size=0,_transferEncoding=0;bool authorized=false;
 public:
 HTTPClient(){++liveHTTP;}~HTTPClient(){--liveHTTP;}
 void setConnectTimeout(unsigned n){assert(n==10000);}void setTimeout(unsigned n){assert(n==15000);}
 void setReuse(bool n){assert(!n);}void setFollowRedirects(int n){assert(n==HTTPC_DISABLE_FOLLOW_REDIRECTS);}
 bool begin(WiFiClientSecure& tls,const String& url){assert(tls.trusted&&url.startsWith("https://"));_client=&tls;return true;}
 void setAuthorization(const char* user,const char* password){assert(user&&*user&&password&&*password);authorized=true;}
 void addHeader(const char*,const String&){}
 int sendRequest(const char* m,const char*){return send(m);}
 int sendRequest(const char* m,uint8_t*,size_t){return send(m);}
 int sendRequest(const char* m,Stream* stream,size_t size){
  const int status=send(m);uint8_t bytes[64];size_t count=0;
  while(count<size&&stream->available()>0){auto n=stream->readBytes(bytes,std::min(sizeof bytes,size-count));if(!n)break;count+=n;}
  return status;
 }
 int GET(){return send("GET");}int getSize(){return _size;}
 int writeToStream(Stream*);
 void end(){}
 private:
 bool connected(){return _client&&(_client->available()>0||_client->connected());}
 int writeToStreamDataBlock(Stream*,int);
 int returnError(int code){return code;}
 void disconnect(bool){if(_client)static_cast<WiFiClientSecure*>(_client)->stop();}
 int send(const char* method){
  assert(authorized&&!bodyReplies.empty());auto r=std::move(bodyReplies.front());bodyReplies.pop_front();assert(r.method==method);bodyMethods.emplace_back(method);
  bodyNetwork={};bodyNetwork.bytes=std::move(r.bytes);bodyNetwork.stayOpen=r.stayOpen;
  bodyNetwork.mode=r.mode;bodyNetwork.poll=bodyPoll;
  _size=r.declared==-2?int(bodyNetwork.bytes.size()):r.declared;_transferEncoding=r.chunked?HTTPC_TE_CHUNKED:HTTPC_TE_IDENTITY;return r.status;
 }
};
