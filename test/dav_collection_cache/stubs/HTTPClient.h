#pragma once
// Reuse the existing bounded HTTP fixture, recording endpoints/headers as well.
#define HTTPClient BaseHTTPFixture
#include "../../backup_integration/dav_stubs/HTTPClient.h"
#undef HTTPClient
#include <map>
namespace collection_test {
struct Request {std::string endpoint;std::map<std::string,std::string> headers;};
inline std::vector<Request> requests;
inline size_t failBodyRequest=0;
inline int bodyError=-4;
}
class HTTPClient:public BaseHTTPFixture {
 public:
  bool begin(WiFiClientSecure& tls,const String& endpoint){
    collection_test::requests.push_back({endpoint,{}});
    return BaseHTTPFixture::begin(tls,endpoint);
  }
  void addHeader(const char* name,const String& value){
    collection_test::requests.back().headers[name]=value;
    BaseHTTPFixture::addHeader(name,value);
  }
  int writeToStream(Stream* stream){
    if(collection_test::failBodyRequest==collection_test::requests.size())return collection_test::bodyError;
    return BaseHTTPFixture::writeToStream(stream);
  }
};
