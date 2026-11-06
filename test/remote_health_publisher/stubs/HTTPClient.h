#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <functional>
#include <vector>

constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
namespace dav_fixture {
inline std::string Base="https://dav.example.test/QA/";
struct Resource {bool collection=false;std::string body,etag;};
struct Request {std::string method,url,body;std::map<std::string,std::string> headers;bool authorized=false,trusted=false,redirects_disabled=false;};
inline std::map<std::string,Resource> resources;
inline std::vector<Request> requests;
inline unsigned liveHTTP=0,maxHTTP=0,beginCount=0,etagSequence=0;
// A successful conditional replacement may store bytes before returning a
// provider-specific status. Unlike forcedStatus, this exercises that outcome.
inline int successfulReplacementStatus=204;
inline bool enforceConditions=true;
inline bool beginOk=true;
inline std::map<unsigned,int> forcedStatus,forcedDeclared;
inline std::map<unsigned,std::string> forcedBody;
inline std::set<unsigned> readFailures;
inline std::function<void(unsigned)> onBegin,onAfterRequest;
inline std::function<void(unsigned,const Request&)> onBeforeRequest;
inline std::function<void(unsigned,size_t)> onRead,onUpload;
inline std::string escape(const std::string& value){std::string out;for(char c:value)out+=c=='&'?"&amp;":c=='<'?"&lt;":c=='>'?"&gt;":std::string(1,c);return out;}
inline std::string properties(const Resource& resource){return "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:propstat><d:prop><d:resourcetype>"+
  std::string(resource.collection?"<d:collection/>":"")+"</d:resourcetype><d:getetag>"+escape(resource.etag)+
  "</d:getetag></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>";}
}

class HTTPClient {
  std::string endpoint_,body_;std::map<std::string,std::string> headers_;
  bool authorized_=false,trusted_=false,redirects_=false;unsigned request_=0;int declared_=0;
 public:
  HTTPClient(){using namespace dav_fixture;++liveHTTP;maxHTTP=std::max(maxHTTP,liveHTTP);assert(liveHTTP<=1);}
  ~HTTPClient(){assert(dav_fixture::liveHTTP);--dav_fixture::liveHTTP;}
  void setConnectTimeout(unsigned timeout){assert(timeout==10000);}
  void setTimeout(unsigned timeout){assert(timeout==15000);}
  void setReuse(bool reuse){assert(!reuse);}
  void setFollowRedirects(int mode){assert(mode==HTTPC_DISABLE_FOLLOW_REDIRECTS);redirects_=true;}
  bool begin(WiFiClientSecure& tls,const String& endpoint){
    using namespace dav_fixture;endpoint_=endpoint;trusted_=tls.trusted;++beginCount;
    assert(endpoint_.rfind(Base,0)==0);if(onBegin)onBegin(beginCount);return beginOk&&tls.trusted;
  }
  void setAuthorization(const char* user,const char* password){
    assert(std::string(user)=="synthetic-user"&&std::string(password)=="synthetic-secret");authorized_=true;
  }
  void addHeader(const char* key,const String& value){headers_[key]=value;}
  int sendRequest(const char* method,const char* value){return send(method,value?std::string(value):std::string());}
  int sendRequest(const char* method,uint8_t* value,size_t length){return send(method,std::string(reinterpret_cast<char*>(value),length));}
  int sendRequest(const char* method,Stream* stream,size_t length){
    using namespace dav_fixture;std::string value;value.reserve(length);uint8_t bytes[16];
    while(value.size()<length&&stream->available()>0){
      if(onUpload)onUpload(unsigned(requests.size()+1),value.size());
      const auto n=stream->readBytes(bytes,std::min(sizeof bytes,length-value.size()));if(!n)break;
      value.append(reinterpret_cast<char*>(bytes),n);
    }
    return send(method,value);
  }
  int GET(){return send("GET",{});}
  int getSize()const{return declared_;}
  int writeToStream(Stream* stream){
    using namespace dav_fixture;size_t done=0;
    while(done<body_.size()){
      if(onRead)onRead(request_,done);
      if(readFailures.count(request_)&&done>=body_.size()/2)return -1;
      const size_t n=std::min(size_t(16),body_.size()-done);
      if(stream->write(reinterpret_cast<const uint8_t*>(body_.data()+done),n)!=n)return -1;
      done+=n;
    }return int(done);
  }
  void end(){}
 private:
  int send(const char* method,const std::string& value){
    using namespace dav_fixture;
    assert(authorized_&&trusted_&&redirects_&&endpoint_.rfind(Base,0)==0);
    Request request{method,endpoint_,value,headers_,authorized_,trusted_,redirects_};
    requests.push_back(request);request_=unsigned(requests.size());
    if(onBeforeRequest)onBeforeRequest(request_,requests.back());
    int status=0;body_.clear();
    if(forcedStatus.count(request_))status=forcedStatus[request_];
    else {
      auto found=resources.find(endpoint_);
      if(request.method=="GET"){
        status=found==resources.end()?404:found->second.collection?405:200;
        if(status==200)body_=found->second.body;
      }else if(request.method=="PROPFIND"){
        status=found==resources.end()?404:207;if(status==207)body_=properties(found->second);
      }else if(request.method=="MKCOL"){
        status=found==resources.end()?201:405;if(status==201)resources[endpoint_]={true,{},"\"collection\""};
      }else if(request.method=="PUT"){
        if(found!=resources.end()&&found->second.collection)status=405;
        else if(enforceConditions&&headers_.count("If-None-Match")&&headers_.at("If-None-Match")=="*"&&found!=resources.end())status=412;
        else if(enforceConditions&&headers_.count("If-Match")&&(found==resources.end()||found->second.etag!=headers_.at("If-Match")))status=412;
        else{
          assert(headers_.count("If-Match")||headers_.count("If-None-Match"));
          status=found==resources.end()?201:successfulReplacementStatus;
          resources[endpoint_]={false,value,"\"e"+std::to_string(++etagSequence)+"\""};
        }
      }else assert(false&&"health must never use MOVE/DELETE/other methods");
    }
    if(forcedBody.count(request_))body_=forcedBody[request_];
    declared_=forcedDeclared.count(request_)?forcedDeclared[request_]:int(body_.size());
    if(onAfterRequest)onAfterRequest(request_);
    return status;
  }
};
