#include "remote_health_publisher.h"
#if CSP_ENABLE_REMOTE_HEALTH
#include "webdav_client.h"
#include "pro_device.h"
#include "http_response_guard.h"
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <limits>
#if !CSP_ENABLE_BACKUP_SETUP
#error Remote health publisher requires the reviewed exclusive combined worker runtime
#endif
namespace {
constexpr size_t HeaderCapacity=224,TagLimit=128;
constexpr uint32_t AttemptMs=30000;
bool cancelled(uint32_t start){return maintenance_active()||uint32_t(millis()-start)>=AttemptMs;}
struct Digest {
  mbedtls_sha256_context value;
  bool ok=true;
  Digest(){mbedtls_sha256_init(&value);ok=mbedtls_sha256_starts(&value,0)==0;}
  ~Digest(){mbedtls_sha256_free(&value);}
  void add(const void* data,size_t n){if(ok)ok=mbedtls_sha256_update(&value,static_cast<const uint8_t*>(data),n)==0;}
  bool finish(char out[65]){unsigned char bytes[32];if(!ok||mbedtls_sha256_finish(&value,bytes))return false;
    static constexpr char Hex[]="0123456789abcdef";for(size_t i=0;i<32;++i){out[i*2]=Hex[bytes[i]>>4];out[i*2+1]=Hex[bytes[i]&15];}out[64]=0;return true;}
};
bool strongTag(const std::string& tag){
  if(tag.size()<2||tag.size()>TagLimit||tag.front()!='"'||tag.back()!='"')return false;
  for(size_t i=1;i+1<tag.size();++i)if(uint8_t(tag[i])<0x21||uint8_t(tag[i])>0x7e||tag[i]=='"')return false;
  return true;
}
bool koofrSinglePublisher(const String& base){
  // Koofr's reviewed endpoint returns 201 for owned replacements and does not
  // enforce HTTP conditional headers. This health-only compatibility mode is
  // for one serialized publisher, not CAS. Require the canonical provider and
  // a safe folder; retain ownership/validators and complete readback below.
  static constexpr char Prefix[]="https://app.koofr.net/dav/Koofr/";
  const char* p=base.c_str();
  if(std::strncmp(p,Prefix,sizeof Prefix-1))return false;
  p+=sizeof Prefix-1;if(!*p)return false;
  bool first=true;
  for(;*p;++p){
    const char c=*p;const bool alnum=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
    if(c=='/'){if(first)return false;first=true;continue;}
    if(!alnum&&(first||(c!='.'&&c!='_'&&c!='-')))return false;
    first=false;
  }
  return first; // Require the canonical trailing slash after a nonempty segment.
}
bool decimal(const char* value,uint64_t& out){
  if(!value||!*value||(value[0]=='0'&&value[1]))return false;out=0;
  for(const char* p=value;*p;++p){if(*p<'0'||*p>'9'||out>(UINT64_MAX-uint64_t(*p-'0'))/10)return false;out=out*10+uint64_t(*p-'0');}return true;
}
class ExactSink:public Stream {
 public:
  ExactSink(const char* expected,size_t n,pro::http_response::Guard& guard):expected_(expected),size_(n),guard_(guard){}
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
  size_t write(uint8_t b)override{return write(&b,1);}
  size_t write(const uint8_t* p,size_t n)override{
    // Print's error flag is cleared by the pinned HTTP short-write retry. Keep
    // our own terminal state and set it again even when the next input is zero.
    if(!ok_||!guard_.ready()||n>size_-count_||std::memcmp(expected_+count_,p,n)){
      ok_=false;setWriteError();return 0;
    }count_+=n;return n;
  }
  bool matches(){return ok_&&count_==size_&&guard_.ready();}
 private:const char* expected_;size_t size_,count_=0;pro::http_response::Guard& guard_;bool ok_=true;
};
class EnvelopeSink:public Stream {
 public:
  EnvelopeSink(const char* owner,pro::http_response::Guard& guard):owner_(owner),guard_(guard){}
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
  size_t write(uint8_t b)override{return write(&b,1);}
  size_t write(const uint8_t* bytes,size_t n)override{
    if(!guard_.ready()||failed_)return reject();
    for(size_t i=0;i<n;++i){++received_;
      if(!owned_){
        if(bytes[i]!='\n'&&(bytes[i]<0x20||bytes[i]>0x7e))return reject();
        if(used_+1>=sizeof header_)return reject();header_[used_++]=char(bytes[i]);header_[used_]=0;
        if(used_>=2&&header_[used_-1]=='\n'&&header_[used_-2]=='\n'&&!parse())return reject();
      }else{
        if(body_>=length_)return reject();hash_.add(bytes+i,1);++body_;
      }
    }return n;
  }
  bool valid(){char actual[65];return owned_&&!failed_&&body_==length_&&hash_.finish(actual)&&!std::strcmp(actual,expected_)&&guard_.ready();}
  bool owned()const{return owned_;}uint64_t revision()const{return revision_;}
  bool truncated(){return owned_&&!failed_&&body_<length_&&guard_.ready();}
  const char* payloadHash()const{return expected_;}size_t received()const{return received_;}
 private:
  size_t reject(){failed_=true;setWriteError();return 0;}
  bool parse(){
    char* lines[6]{};size_t count=0;lines[count++]=header_;
    for(size_t i=0;i<used_;++i)if(header_[i]=='\n'){header_[i]=0;if(count<6)lines[count++]=header_+i+1;}
    if(count!=6||std::strcmp(lines[0],"CSP-HEALTH/1")||std::strncmp(lines[1],"owner=",6)||std::strcmp(lines[1]+6,owner_)||
       std::strncmp(lines[2],"revision=",9)||std::strncmp(lines[3],"length=",7)||std::strncmp(lines[4],"sha256=",7)||*lines[5])return false;
    uint64_t length=0;if(!decimal(lines[2]+9,revision_)||!revision_||!decimal(lines[3]+7,length)||!length||length>=pro::health::ReportCapacity)return false;
    if(!pro::validHex(lines[4]+7))return false;std::memcpy(expected_,lines[4]+7,65);length_=size_t(length);owned_=true;return true;
  }
  const char* owner_;pro::http_response::Guard& guard_;char header_[HeaderCapacity]{};char expected_[65]{};
  size_t used_=0,body_=0,length_=0,received_=0;uint64_t revision_=0;bool owned_=false,failed_=false;Digest hash_;
};
class MemoryStream:public Stream {
 public:
  MemoryStream(const char* header,size_t h,const char* report,size_t n,uint32_t start):header_(header),report_(report),h_(h),n_(n),start_(start){}
  int available()override{return cancelled(start_)?0:int(h_+n_-done_);}
  int read()override{if(!available())return -1;const auto p=done_++;return uint8_t(p<h_?header_[p]:report_[p-h_]);}
  int peek()override{return available()?uint8_t(done_<h_?header_[done_]:report_[done_-h_]):-1;}
  void flush()override{}size_t write(uint8_t)override{return 0;}
  size_t readBytes(char* target,size_t n)override{size_t i=0;int b;while(i<n&&(b=read())>=0)target[i++]=char(b);return i;}
  size_t readBytes(uint8_t* target,size_t n)override{return readBytes(reinterpret_cast<char*>(target),n);}
  bool complete()const{return done_==h_+n_;}
 private:const char* header_;const char* report_;size_t h_,n_,done_=0;uint32_t start_;
};
struct Slot {bool present=false,valid=false,owned=false;uint64_t revision=0;std::string etag;};
}

pro::health::Publication WebDAVClient::publishHealth(const pro::health::Snapshot& observation,const pro::health::Admission& admission){
  using namespace pro::health;
  const uint32_t started=millis();
  auto failure=[](int32_t code){return Publication{false,code,0,0};};
  if(!admission.normal_worker||!admission.sd_view_unmounted||!admission.radio_exclusive||maintenance_active())return failure(HealthDenied);
  if(!config_.is_valid||!config_.server_url.startsWith("https://"))return failure(HealthInvalid);
  Snapshot report=observation;report.sequence=1;
  char payload[ReportCapacity];size_t length=0;if(!encode(report,payload,sizeof payload,length))return failure(HealthInvalid);
  auto allowed=[&](){return !cancelled(started);};
  const auto scope=hash_text("CSP-health-v1\n"+std::to_string(ESP.getEfuseMac())+"\n"+config_.server_url.c_str()+"\n"+config_.username.c_str());
  if(!pro::validHex(scope.c_str()))return failure(HealthInvalid);
  const std::string folder="._csp_health_v1_"+scope+"/",ownerPath=folder+"owner.txt";
  const std::string marker="CSP-HEALTH-OWNER/1\n"+scope+"\n";
  auto cancelledResult=[&](){return failure(maintenance_active()?pro::SnapshotChanged:HealthDeadline);};
  const auto retirement=+[](void*){return maintenance_active();};
  auto bodyError=[](const pro::http_response::Guard& guard){
    return guard.error()==pro::http_response::Guard::DeadlineError?HealthDeadline:guard.error();
  };
  auto markerRead=[&]()->pro::Result{
    if(!allowed())return {false,pro::SnapshotChanged};
    pro::http_response::Guard guard(retirement,nullptr,started,AttemptMs);
    pro::http_response::TLS tls;HTTPClient http;
    if(!begin(http,tls,url(ownerPath)))return {false,HealthDenied};if(!allowed())return {false,pro::SnapshotChanged};
    http.addHeader("Accept-Encoding","identity");const int code=http.GET();
    if(code!=200)return {false,code};const int declared=http.getSize();
    if(declared>=0&&size_t(declared)!=marker.size())return {false,HealthMalformed};
    tls.arm(guard);ExactSink sink(marker.data(),marker.size(),guard);
    const int read=http.writeToStream(&sink);http.end();
    if(guard.error())return {false,bodyError(guard)};
    const bool match=read>=0&&(declared<0||read==declared)&&sink.matches();return {match,match?200:HealthCollision};
  };
  auto marked=markerRead();
  if(!allowed())return cancelledResult();
  if(!marked.ok){
    if(marked.code!=404)return failure(marked.code);
    {WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(folder)))return failure(HealthDenied);
      if(!allowed())return cancelledResult();const int code=http.sendRequest("MKCOL","");http.end();
      if(!allowed())return cancelledResult();if(code!=201)return failure(code==405?HealthCollision:code);}
    {WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(ownerPath)))return failure(HealthDenied);
      http.addHeader("If-None-Match","*");http.addHeader("Content-Type","text/plain; charset=utf-8");
      if(!allowed())return cancelledResult();const int code=http.sendRequest("PUT",reinterpret_cast<uint8_t*>(const_cast<char*>(marker.data())),marker.size());http.end();
      if(!allowed())return cancelledResult();if(code!=201)return failure(code);}
    marked=markerRead();if(!allowed())return cancelledResult();if(!marked.ok)return failure(marked.code);
  }
  Slot slots[2];
  auto slotPath=[&](unsigned slot){return folder+(slot?"status-b.txt":"status-a.txt");};
  for(unsigned i=0;i<2;++i){
    if(!allowed())return cancelledResult();auto result=destinationValidator(slotPath(i),slots[i].etag);
    if(!allowed())return cancelledResult();if(!result.ok)return failure(result.code);
    if(result.code==404)continue;slots[i].present=true;if(!strongTag(slots[i].etag))return failure(HealthCollision);
    {pro::http_response::Guard guard(retirement,nullptr,started,AttemptMs);
      pro::http_response::TLS tls;HTTPClient http;if(!begin(http,tls,url(slotPath(i))))return failure(HealthDenied);
      http.addHeader("Accept-Encoding","identity");if(!allowed())return cancelledResult();const int code=http.GET();
      if(code!=200)return failure(code);const int declared=http.getSize();if(declared>int(HeaderCapacity+ReportCapacity))return failure(HealthMalformed);
      tls.arm(guard);EnvelopeSink sink(scope.c_str(),guard);const int read=http.writeToStream(&sink);http.end();
      if(guard.error())return failure(bodyError(guard));
      if(!allowed())return cancelledResult();if(read<0||(declared>=0&&read!=declared))return failure(HealthMalformed);
      slots[i].owned=sink.owned();slots[i].valid=sink.valid();slots[i].revision=sink.revision();
      if(!slots[i].owned)return failure(HealthCollision);
      if(!slots[i].valid&&!sink.truncated())return failure(HealthMalformed);}
  }
  unsigned selected=!slots[0].present?0:!slots[1].present?1:!slots[0].valid?0:!slots[1].valid?1:slots[0].revision<slots[1].revision?0:1;
  if(slots[0].valid&&slots[1].valid&&slots[0].revision==slots[1].revision)return failure(HealthCollision);
  const auto previous=std::max(slots[0].revision,slots[1].revision);if(previous==UINT64_MAX)return failure(HealthInvalid);
  report.sequence=previous+1;if(!encode(report,payload,sizeof payload,length))return failure(HealthInvalid);
  char hash[65];{Digest digest;digest.add(payload,length);if(!digest.finish(hash))return failure(HealthInvalid);}
  char header[HeaderCapacity];const int hn=std::snprintf(header,sizeof header,"CSP-HEALTH/1\nowner=%s\nrevision=%llu\nlength=%u\nsha256=%s\n\n",scope.c_str(),static_cast<unsigned long long>(report.sequence),unsigned(length),hash);
  if(hn<=0||size_t(hn)>=sizeof header)return failure(HealthInvalid);
  {WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(slotPath(selected))))return failure(HealthDenied);
    http.addHeader(slots[selected].present?"If-Match":"If-None-Match",slots[selected].present?String(slots[selected].etag.c_str()):String("*"));
    http.addHeader("Content-Type","text/plain; charset=utf-8");if(!allowed())return cancelledResult();
    MemoryStream stream(header,size_t(hn),payload,length,started);const int code=http.sendRequest("PUT",&stream,size_t(hn)+length);http.end();
    if(!allowed())return cancelledResult();
    const bool expected_status=slots[selected].present?
      (code==200||code==204||(code==201&&koofrSinglePublisher(config_.server_url))):code==201;
    if(!expected_status)return failure(code>=200&&code<300?HealthCollision:code);
    if(!stream.complete())return failure(HealthMalformed);}
  {pro::http_response::Guard guard(retirement,nullptr,started,AttemptMs);
    pro::http_response::TLS tls;HTTPClient http;if(!begin(http,tls,url(slotPath(selected))))return failure(HealthDenied);
    http.addHeader("Accept-Encoding","identity");if(!allowed())return cancelledResult();const int code=http.GET();if(code!=200)return failure(code);
    const int declared=http.getSize();if(declared>int(HeaderCapacity+ReportCapacity))return failure(HealthMalformed);
    tls.arm(guard);EnvelopeSink sink(scope.c_str(),guard);const int read=http.writeToStream(&sink);http.end();
    if(guard.error())return failure(bodyError(guard));
    if(!allowed())return cancelledResult();if(read<0||(declared>=0&&read!=declared)||!sink.valid()||sink.revision()!=report.sequence||std::strcmp(sink.payloadHash(),hash))return failure(HealthMalformed);}
  return {true,200,report.sequence,uint8_t(selected)};
}
#endif
