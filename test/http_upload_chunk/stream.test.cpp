// The included method is extracted verbatim from pinned/production source.
// Clock, HTTP header/response, source and TCP writes are simulated; no sockets.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

constexpr int HTTPC_ERROR_CONNECTION_REFUSED=-1, HTTPC_ERROR_SEND_HEADER_FAILED=-2;
constexpr int HTTPC_ERROR_SEND_PAYLOAD_FAILED=-3, HTTPC_ERROR_NO_STREAM=-6;
constexpr int HTTPC_ERROR_TOO_LESS_RAM=-8;
#define HTTP_TCP_TX_BUFFER_SIZE CSP_TEST_CHUNK
#define log_d(...) ((void)0)
static size_t liveAllocations=0, allocationSize=0, allocationCalls=0;
static bool failAllocation=false;
static unsigned delayZero=0, delayOne=0;
static std::function<void(unsigned)> afterDelay;
void* tracked_alloc(size_t size){
  ++allocationCalls;allocationSize=size;
  if(failAllocation)return nullptr;
  void* memory=std::malloc(size);assert(memory);++liveAllocations;return memory;
}
void tracked_free(void* memory){if(memory){assert(liveAllocations);--liveAllocations;std::free(memory);}}
void delay(unsigned ms){assert(ms<=1);if(ms)++delayOne;else ++delayZero;if(afterDelay)afterDelay(ms);assert(delayOne+delayZero<1000000);}
class String:public std::string {public:using std::string::string;String(size_t value):std::string(std::to_string(value)){};String()=default;};
class Stream {
 public:virtual ~Stream()=default;virtual int available()=0;virtual size_t readBytes(uint8_t*,size_t)=0;
};
struct Peer {
  std::vector<uint8_t> output;
  std::vector<size_t> requested;
  std::deque<size_t> accepts;
  unsigned calls=0;int error=0;bool connected=true;
  std::function<void(unsigned)> afterWrite;
  size_t write(const uint8_t* data,size_t size){
    assert(size<=size_t(CSP_TEST_CHUNK));requested.push_back(size);
    size_t n=size;if(!accepts.empty()){n=std::min(size,accepts.front());accepts.pop_front();}
    output.insert(output.end(),data,data+n);++calls;if(afterWrite)afterWrite(calls);return n;
  }
  int getWriteError(){return error;}void clearWriteError(){error=0;}
};
class HTTPClient {
 protected:
  Peer* _client;
  bool connect(){return connectionAllowed;}
  bool sendHeader(const char* method){assert(std::string(method)=="PUT");++headerCalls;return headerAllowed;}
  int handleHeaderResponse(){++responseCalls;return response;}
  int returnError(int code){return code;}
  bool generateCookieString(String*){return false;}
 public:
  bool connectionAllowed=true,headerAllowed=true;
  int headerCalls=0,responseCalls=0,response=201;
  size_t contentLength=0;
  explicit HTTPClient(Peer& peer):_client(&peer){}
  bool connected(){return _client->connected;}
  void addHeader(const char* name,const String& value){assert(std::string(name)=="Content-Length");contentLength=std::stoull(value);}
  int sendRequest(const char* type,Stream* stream,size_t size);
};
#define UploadHTTPClient HTTPClient
#define malloc tracked_alloc
#define free tracked_free
#include "actual_stream_method.inc"
#undef free
#undef malloc
#undef UploadHTTPClient

struct Source:Stream {
  std::vector<uint8_t> data;
  size_t offset=0,maxRead=0,readCalls=0;
  unsigned availableCalls=0;
  bool cancelled=false,negativeRace=false,zeroRead=false,partialRead=false;
  bool oneEmptyPoll=false;
  std::function<void(unsigned)> onAvailable;
  explicit Source(size_t n):data(n){for(size_t i=0;i<n;++i)data[i]=uint8_t(i*37U+i/251U);}
  int available()override{
    ++availableCalls;if(onAvailable)onAvailable(availableCalls);
    if(cancelled||(negativeRace&&availableCalls==2))return -1;
    if(oneEmptyPoll&&availableCalls==2)return 0;
    if(offset==data.size())return -1;
    return int(data.size()-offset);
  }
  size_t readBytes(uint8_t* bytes,size_t size)override{
    ++readCalls;maxRead=std::max(maxRead,size);
    // A real available() revocation at the second observation must never pass
    // SIZE_MAX here. The unmodified pinned method triggers ASan at this call.
    std::memset(bytes,0,size);
    assert(size<=size_t(CSP_TEST_CHUNK));
    if(zeroRead)return 0;
    size_t n=std::min(size,data.size()-offset);if(partialRead&&n>1)--n;
    std::memcpy(bytes,data.data()+offset,n);offset+=n;return n;
  }
};
static unsigned cases=0;
static void reset(){assert(liveAllocations==0);allocationSize=allocationCalls=delayZero=delayOne=0;failAllocation=false;afterDelay={};}
static void pass(const char* name){assert(liveAllocations==0);++cases;std::cout<<"PASS "<<name<<'\n';}
static void rejected(HTTPClient& http,Source& source,Peer& peer){
  assert(http.sendRequest("PUT",&source,source.data.size())==HTTPC_ERROR_SEND_PAYLOAD_FAILED);
  assert(http.responseCalls==0);assert(peer.output.size()<source.data.size());
}
static void race(){
  reset();Peer peer;Source source(8192);HTTPClient http(peer);source.negativeRace=true;
  rejected(http,source,peer);assert(source.readCalls==0&&peer.calls==0);
  pass("second available() cancellation rejects before read or write");
}
int main(int argc,char** argv){
  if(argc==2&&std::string(argv[1])=="--available-race"){race();return 0;}
  for(size_t bytes:{1U,1459U,1460U,1461U,4095U,4096U,4097U,2U*1024U*1024U+137U}){
    reset();Peer peer;Source source(bytes);HTTPClient http(peer);
    assert(http.sendRequest("PUT",&source,bytes)==201);assert(http.responseCalls==1);
    assert(peer.output==source.data&&source.offset==bytes);
    assert(allocationSize==std::min(size_t(CSP_TEST_CHUNK),bytes)&&allocationCalls==1);
    assert(source.maxRead<=allocationSize&&delayZero==source.readCalls);
    pass("complete boundary/2MiB source is byte-identical and yields each chunk");
  }
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);peer.accepts={31};
    assert(http.sendRequest("PUT",&source,source.data.size())==201);assert(peer.output==source.data&&delayOne==1);
  }pass("one short write retries exact remaining bytes without duplication");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);peer.accepts={31,0};rejected(http,source,peer);}
  pass("second short write rejects and frees buffer before response");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);peer.afterWrite=[&](unsigned){peer.error=1;};
    assert(http.sendRequest("PUT",&source,source.data.size())==HTTPC_ERROR_SEND_PAYLOAD_FAILED&&http.responseCalls==0);
  }pass("transport write error is terminal");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);peer.accepts={31};peer.afterWrite=[&](unsigned n){if(n==1)peer.error=1;};
    assert(http.sendRequest("PUT",&source,source.data.size())==201);assert(peer.output==source.data&&peer.error==0);
  }pass("SDK clear-write-error/retry policy retained for a recoverable short write");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);peer.afterWrite=[&](unsigned){peer.connected=false;};rejected(http,source,peer);}
  pass("disconnect during stream cannot reach successful response");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);afterDelay=[&](unsigned){source.cancelled=true;};rejected(http,source,peer);}
  pass("source cancellation after yielded chunk is terminal");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);source.oneEmptyPoll=true;
    assert(http.sendRequest("PUT",&source,source.data.size())==201&&peer.output==source.data&&delayOne==1);
  }pass("temporarily empty source preserves yielding and subsequent transfer");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);source.partialRead=true;rejected(http,source,peer);}
  pass("premature short source never reaches a false complete response");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);source.zeroRead=true;rejected(http,source,peer);}
  pass("zero-byte read failure cannot be accepted as a completed payload");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);failAllocation=true;
    assert(http.sendRequest("PUT",&source,source.data.size())==HTTPC_ERROR_TOO_LESS_RAM&&source.readCalls==0&&peer.calls==0);
  }pass("allocation failure rejects before reading source");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);http.connectionAllowed=false;
    assert(http.sendRequest("PUT",&source,source.data.size())==HTTPC_ERROR_CONNECTION_REFUSED&&allocationCalls==0);
  }pass("connection refusal allocates no stream buffer");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);http.headerAllowed=false;
    assert(http.sendRequest("PUT",&source,source.data.size())==HTTPC_ERROR_SEND_HEADER_FAILED&&allocationCalls==0);
  }pass("header rejection allocates no stream buffer");
  reset();{Peer peer;HTTPClient http(peer);assert(http.sendRequest("PUT",nullptr,1)==HTTPC_ERROR_NO_STREAM&&allocationCalls==0);}
  pass("null source rejected before any connection or allocation");
  reset();{Peer peer;Source source(8192);HTTPClient http(peer);
    assert(http.sendRequest("PUT",&source,0)==201&&peer.output==source.data);
  }pass("unknown-length sentinel stream preserves original framing behavior");
#if CSP_TEST_PATCHED
  race();
#endif
  std::cout<<cases<<" actual stream method case groups passed; chunk="<<CSP_TEST_CHUNK<<"\n";
}
