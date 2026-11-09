#include "webdav_client.h"
#include "pro_device.h"
#include "usb_storage.h"
#include "pro_dav_properties.h"
#include <mbedtls/sha256.h>
#include <WiFi.h>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <utility>
#include <memory>
#include <esp_random.h>
#include "koofr_ca.h"
#include "upload_diagnostics.h"
#include "http_response_guard.h"
#include "upload_chunk_trial.h"
#if CSP_UPLOAD_DIAGNOSTICS
#include <cerrno>
// Only the existing PUT clients expose their already-present numeric socket
// timeout. All connection, certificate and hostname behavior remains inherited.
class UploadTLS:public WiFiClientSecure {
 public:uint32_t diagnosticTimeout()const{return sslclient->socket_timeout;}
};
#else
using UploadTLS=WiFiClientSecure;
#endif
static bool sourceReady(){return !maintenance_active()&&storage_ready();}
// The combined runtime retires the backup worker before lending memory to BLE.
// Existing blocking requests retain their finite transport timeouts; a retired
// worker must not start the next request in a metadata/upload/readback chain.
static bool transportCancelled(){
#if CSP_ENABLE_BACKUP_SETUP
  return maintenance_active();
#else
  return false;
#endif
}
static int beginError(){return transportCancelled()?pro::SnapshotChanged:-2001;}
template<class Client,class... Args>
static int request(Client& http,const char* method,Args&&... args){
  if(transportCancelled())return pro::SnapshotChanged;
  return http.sendRequest(method,std::forward<Args>(args)...);
}
static int get(HTTPClient& http){return transportCancelled()?pro::SnapshotChanged:http.GET();}
// Pinned HTTPClient's body loop has no idle deadline. In particular, a secure
// read can return zero while TCP remains connected; 0 written for 0 read is not
// an error there. Unknown-length idle bodies never call the destination sink.
// Keep framing/unchunking in HTTPClient, and bound both paths through its
// virtual TLS read interface. Certificate/hostname validation stays inherited.
using ResponseBodyGuard=pro::http_response::Guard;
using ResponseTLS=pro::http_response::TLS;
static_assert(ResponseBodyGuard::CancelledError==pro::SnapshotChanged,"Body cancellation code must remain SnapshotChanged");
static bool responseCancelled(void* guarded){
  return transportCancelled()||(guarded&&*static_cast<const bool*>(guarded)&&!sourceReady());
}
static std::string hexDigest(unsigned char* bytes) {
  static const char hex[]="0123456789abcdef";std::string out;out.reserve(64);for(int i=0;i<32;++i){out+=hex[bytes[i]>>4];out+=hex[bytes[i]&15];}return out;
}
std::string hash_text(const std::string& s) {
  unsigned char b[32];if(mbedtls_sha256(reinterpret_cast<const unsigned char*>(s.data()),s.size(),b,0)!=0)return "";return hexDigest(b);
}
bool hash_file(const std::string& path,std::string& out,uint64_t& size) {
  if(!sourceReady()||!pro::validPath(path))return false;
  FILE* f=fopen(("/sdcard/"+path).c_str(),"rb");if(!f)return false;
  if(fseek(f,0,SEEK_END)!=0){fclose(f);return false;}long expected=ftell(f);if(expected<0){fclose(f);return false;}rewind(f);backup_transfer_begin(pro::TransferPhase::Reading,expected>0?uint64_t(expected):0);
  uint8_t buf[pro::ChunkSize];mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);bool ok=mbedtls_sha256_starts(&ctx,0)==0;size=0;size_t n;
  while(ok&&sourceReady()&&(n=fread(buf,1,sizeof buf,f))>0) {ok=mbedtls_sha256_update(&ctx,buf,n)==0;size+=n;backup_progress(size,expected>0?uint64_t(expected):0);vTaskDelay(1);}
  if(!sourceReady()||size!=uint64_t(expected))ok=false;
  if(ferror(f))ok=false;fclose(f);unsigned char digest[32];if(mbedtls_sha256_finish(&ctx,digest)!=0)ok=false;mbedtls_sha256_free(&ctx);if(ok)out=hexDigest(digest);return ok;
}
String WebDAVClient::url(const std::string& path)const {
  String base=config_.server_url;if(!base.endsWith("/"))base+='/';return base+pro::encodePath(path).c_str();
}
std::string WebDAVClient::stagingDirectory()const{char name[40];snprintf(name,sizeof name,"._csp_uploads_%012llx",(unsigned long long)ESP.getEfuseMac());return name;}
std::string WebDAVClient::stagedPath(const std::string& path)const{return stagingDirectory()+"/"+hash_text(path)+".part";}
bool WebDAVClient::begin(HTTPClient& http,WiFiClientSecure& tls,const String& endpoint) {
  if(transportCancelled()||WiFi.status()!=WL_CONNECTED || time(nullptr)<1767225600)return false;
  tls.setCACert(config_.ca_pem.c_str());tls.setHandshakeTimeout(10);tls.setTimeout(15000);
  http.setConnectTimeout(10000);http.setTimeout(15000);http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(tls,endpoint))return false;
  http.setAuthorization(config_.username.c_str(),config_.password.c_str());return true;
}
class FileStream:public Stream {
 public:
  FileStream(FILE* file,uint64_t size):f(file),total(size){backup_transfer_begin(pro::TransferPhase::Uploading,size);}
  int available()override{
#if CSP_UPLOAD_DIAGNOSTICS
    if(!sourceReady()){upload_diagnostics_source_abort(UploadSourceAbort::Unavailable);return -1;}
    if(ferror(f)){upload_diagnostics_source_abort(UploadSourceAbort::ReadError);return -1;}
    if(feof(f)){if(done<total)upload_diagnostics_source_abort(UploadSourceAbort::EarlyEof);return -1;}
    if(millis()-started>1800000){upload_diagnostics_source_abort(UploadSourceAbort::Deadline);return -1;}
    return int(std::min<uint64_t>(INT32_MAX,total-done));
#else
    return (!sourceReady()||ferror(f)||feof(f)||millis()-started>1800000)?-1:int(std::min<uint64_t>(INT32_MAX,total-done));
#endif
  }
  int read()override{if(!sourceReady())return -1;int c=fgetc(f);if(!sourceReady())return -1;if(c!=EOF){++done;backup_progress(done,total);}return c==EOF?-1:c;}
  int peek()override{if(!sourceReady())return -1;int c=fgetc(f);if(c!=EOF)ungetc(c,f);return c==EOF?-1:c;}
  void flush()override{}
  size_t write(uint8_t)override{return 0;}
  size_t readBytes(char* b,size_t n)override{
#if CSP_UPLOAD_DIAGNOSTICS
    if(!sourceReady()){upload_diagnostics_source_abort(UploadSourceAbort::Unavailable);return 0;}
    const uint32_t before=millis();size_t got=fread(b,1,n,f);const int saved=errno;
    const uint32_t elapsed=millis()-before;const bool ready=sourceReady();
    upload_diagnostics_source_read(n,got,elapsed,saved,ferror(f)!=0,feof(f)!=0,ready);
    errno=saved;if(!ready)return 0;done+=got;backup_progress(done,total);vTaskDelay(1);return got;
#else
    if(!sourceReady())return 0;size_t got=fread(b,1,n,f);if(!sourceReady())return 0;done+=got;backup_progress(done,total);vTaskDelay(1);return got;
#endif
  }
  size_t readBytes(uint8_t* b,size_t n)override{return readBytes(reinterpret_cast<char*>(b),n);}
  uint64_t count()const{return done;}
  bool timedOut()const{return millis()-started>1800000;}
 private:
  FILE* f;uint64_t total,done=0;uint32_t started=millis();
};
class HashSink:public Stream {
 public:
  explicit HashSink(uint64_t expected,ResponseBodyGuard& budget,bool guard=false):limit(expected),guarded(guard),budget_(budget){mbedtls_sha256_init(&ctx);ok=mbedtls_sha256_starts(&ctx,0)==0;if(guarded)backup_transfer_begin(pro::TransferPhase::Checking,expected);}
  ~HashSink(){mbedtls_sha256_free(&ctx);}
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
  size_t write(uint8_t b)override{return write(&b,1);}
  size_t write(const uint8_t* b,size_t n)override {
    if(n>limit-count){overflow=true;return fail();}
    if(!ok||!budget_.ready())return fail();
    if(mbedtls_sha256_update(&ctx,b,n)!=0)return fail();
    count+=n;budget_.progress(n);if(guarded)backup_progress(count,limit);vTaskDelay(1);return n;
  }
  bool matches(const std::string& hash){unsigned char b[32];return ok&&budget_.ready()&&count==limit&&mbedtls_sha256_finish(&ctx,b)==0&&hexDigest(b)==hash;}
  bool tooLarge()const{return overflow;}
 private:
  size_t fail(){ok=false;setWriteError();return 0;}
  mbedtls_sha256_context ctx;uint64_t count=0,limit;bool ok,overflow=false,guarded=false;ResponseBodyGuard& budget_;
};
class PropertySink:public Stream {
 public:
  std::string body;
  explicit PropertySink(ResponseBodyGuard& budget):budget_(budget){}
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
  size_t write(uint8_t b)override{return write(&b,1);}
  size_t write(const uint8_t* b,size_t n)override{
    if(failed_||!budget_.ready()||n>4096-body.size()){failed_=true;setWriteError();return 0;}
    body.append(reinterpret_cast<const char*>(b),n);budget_.progress(n);return n;
  }
 private:ResponseBodyGuard& budget_;bool failed_=false;
};
pro::Result WebDAVClient::destinationValidator(const std::string& path,std::string& etag){
  ResponseBodyGuard budget(responseCancelled,nullptr,millis(),1800000);ResponseTLS tls;HTTPClient http;if(!begin(http,tls,url(path)))return {false,beginError()};
  http.addHeader("Depth","0");http.addHeader("Content-Type","application/xml");
  int code=request(http,"PROPFIND","<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getetag/></d:prop></d:propfind>");
  if(transportCancelled()){http.end();return {false,pro::SnapshotChanged};}
  if(code==404){http.end();etag.clear();return {true,404};}
  if(code!=207){http.end();return {false,code};}
  if(http.getSize()>4096){http.end();return {false,-2011};}
  tls.arm(budget);PropertySink sink(budget);int read=http.writeToStream(&sink);http.end();
  if(budget.error())return {false,budget.error()};
  if(transportCancelled())return {false,pro::SnapshotChanged};
  bool file=read>=0&&pro::fileProperties(sink.body,etag);
  return {file,file?207:-2011};
}
pro::Result WebDAVClient::put(const std::string& path,uint64_t size) {
  auto result=putFile(path,size);
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  if(!result.ok)forgetCollections();
#endif
  return result;
}
pro::Result WebDAVClient::ensureParents(const std::string& path) {
  if(!sourceReady())return {false,pro::SnapshotChanged};
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  const auto last=path.rfind('/');
  if(last!=std::string::npos&&std::strlen(parent_)==last+1&&
     std::memcmp(parent_,path.data(),last+1)==0)return {true,0};
#endif
  // A 405 is tolerated only as an advisory collection result, exactly as in
  // the uncached path. It never proves a file upload or its contents.
  size_t slash=0;
  while((slash=path.find('/',slash))!=std::string::npos) {
    WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(path.substr(0,slash+1))))return {false,beginError()};
    int code=request(http,"MKCOL","");http.end();if(transportCancelled())return {false,pro::SnapshotChanged};if(code!=201&&code!=405)return {false,code};++slash;
  }
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  const auto count=last==std::string::npos?0:last+1;
  std::memcpy(parent_,path.data(),count);parent_[count]='\0';
#endif
  return {true,0};
}
pro::Result WebDAVClient::putFile(const std::string& path,uint64_t size) {
  if(!sourceReady())return {false,pro::SnapshotChanged};
  if(!pro::validPath(path)||size>INT32_MAX)return {false,-2000};
  // Parent collections must exist before PUT. A 405 is tolerated only here;
  // successful PUT+readback is still required and proves the final resource exists.
  auto parents=ensureParents(path);if(!parents.ok)return parents;
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  if(!stagingReady_)
#endif
  {
    WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(stagingDirectory()+"/")))return {false,beginError()};
    int code=request(http,"MKCOL","");http.end();if(transportCancelled())return {false,pro::SnapshotChanged};if(code!=201&&code!=405)return {false,code};
#if CSP_ENABLE_DAV_COLLECTION_CACHE
    stagingReady_=true;
#endif
  }
  if(!sourceReady())return {false,pro::SnapshotChanged};
  FILE* f=fopen(("/sdcard/"+path).c_str(),"rb");if(!f)return {false,-2003};
  UploadTLS tls;UploadHTTPClient http;
  if(!begin(http,tls,url(stagedPath(path)))){fclose(f);return {false,beginError()};}
  http.addHeader("Content-Type","application/octet-stream");
#if CSP_UPLOAD_DIAGNOSTICS
  UploadDiagnosticAttempt diagnostic(size,UploadDiagnosticTarget::Stage);
#endif
  FileStream stream(f,size);
  // Stream size=0 means unknown to HTTPClient; explicitly frame an empty PUT.
  if(!size)http.addHeader("Content-Length","0");
  int code=size?request(http,"PUT",&stream,size):request(http,"PUT",reinterpret_cast<uint8_t*>(const_cast<char*>("")),0);
  bool readError=ferror(f),complete=stream.count()==size&&!readError;bool timeout=stream.timedOut();
#if CSP_UPLOAD_DIAGNOSTICS
  diagnostic.finish(code,tls.diagnosticTimeout(),readError,feof(f)!=0);
#endif
  http.end();fclose(f);
  if(!sourceReady())return {false,pro::SnapshotChanged};
  return {complete&&(code==200||code==201||code==204),complete?code:readError?-2003:timeout?-2012:code<0?code:-2012};
}
pro::Result WebDAVClient::verify(const std::string& path,const std::string& hash,uint64_t size) {
  auto result=verifyFile(path,hash,size);
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  if(!result.ok)forgetCollections();
#endif
  return result;
}
pro::Result WebDAVClient::verifyFile(const std::string& path,const std::string& hash,uint64_t size) {
  if(!sourceReady())return {false,pro::SnapshotChanged};
  if(!pro::validPath(path))return {false,-2000};
  auto checked=verifyURL(url(stagedPath(path)),hash,size,true);
  // A previous MOVE may have committed before a reset; legacy transfers also
  // used the final path directly. Accept either only after full hash readback.
  if(checked.code==404)return verifyURL(url(path),hash,size,true);
  if(!checked.ok)return checked;
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  // A destination parent can disappear after the PUT. A failed MOVE discards
  // the cache, and the next ordinary verify retry rechecks/recreates parents
  // before publication, without reuploading or weakening destination guards.
  auto parents=ensureParents(path);if(!parents.ok)return parents;
#endif
  std::string etag;auto destination=destinationValidator(path,etag);
  if(!destination.ok)return destination;
  if(!sourceReady())return {false,pro::SnapshotChanged};
  // Koofr rejects tagged MOVE conditions. Unconditional MOVE can delete a
  // destination collection, so never use that workaround. PUT rejects a
  // collection on this provider; the SD generation guard and final hash check
  // still apply. Local files are authoritative for ordinary-file replacements.
  if(!etag.empty()&&config_.server_url.startsWith("https://app.koofr.net/")){
    FILE* f=fopen(("/sdcard/"+path).c_str(),"rb");if(!f)return {false,-2003};
    UploadTLS tls;UploadHTTPClient http;if(!begin(http,tls,url(path))){fclose(f);return {false,beginError()};}
    http.addHeader("Content-Type","application/octet-stream");
#if CSP_UPLOAD_DIAGNOSTICS
    UploadDiagnosticAttempt diagnostic(size,UploadDiagnosticTarget::Replacement);
#endif
    FileStream stream(f,size);
    if(!size)http.addHeader("Content-Length","0");
    int code=size?request(http,"PUT",&stream,size):request(http,"PUT",reinterpret_cast<uint8_t*>(const_cast<char*>("")),0);
    bool readError=ferror(f),complete=stream.count()==size&&!readError;bool timeout=stream.timedOut();
#if CSP_UPLOAD_DIAGNOSTICS
    diagnostic.finish(code,tls.diagnosticTimeout(),readError,feof(f)!=0);
#endif
    http.end();fclose(f);
    if(!sourceReady())return {false,pro::SnapshotChanged};
    if(!complete||!(code==200||code==201||code==204))return {false,complete?code:readError?-2003:timeout?-2012:code<0?code:-2012};
    auto final=verifyURL(url(path),hash,size,true);
    if(final.ok){WiFiClientSecure cleanupTls;HTTPClient cleanup;if(begin(cleanup,cleanupTls,url(stagedPath(path)))){request(cleanup,"DELETE","");cleanup.end();}}
    if(transportCancelled())return {false,pro::SnapshotChanged};
    return final;
  }
  {
    WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(stagedPath(path))))return {false,beginError()};
    http.addHeader("Destination",url(path));http.addHeader("Overwrite",etag.empty()?"F":"T");
    // Protect an existing ordinary file against changes since its type check.
    backup_transfer_begin(pro::TransferPhase::Publishing,size);
    if(!etag.empty())http.addHeader("If",String("<")+url(path)+"> (["+etag.c_str()+"])");
    int code=request(http,"MOVE","");http.end();if(transportCancelled())return {false,pro::SnapshotChanged};if(code!=201&&code!=204)return {false,code};
  }
  return verifyURL(url(path),hash,size,true);
}
pro::Result WebDAVClient::verifyURL(const String& endpoint,const std::string& hash,uint64_t size,bool guarded) {
  if(guarded&&!sourceReady())return {false,pro::SnapshotChanged};
  ResponseBodyGuard budget(responseCancelled,&guarded,millis(),1800000);ResponseTLS tls;HTTPClient http;if(!begin(http,tls,endpoint))return {false,beginError()};
  // Stream/unchunk through HTTPClient, not its raw socket. No whole-response allocation.
  http.addHeader("Accept-Encoding","identity");int code=get(http);
  if(transportCancelled()){http.end();return {false,pro::SnapshotChanged};}
  if(code!=200){http.end();return {false,code};}
  int declared=http.getSize();if(declared>=0&&uint64_t(declared)!=size){http.end();return {false,pro::DigestMismatch};}
  tls.arm(budget);HashSink sink(size,budget,guarded);int written=http.writeToStream(&sink);bool match=written>=0&&sink.matches(hash);http.end();
  if(budget.error())return {false,budget.error()};
  if(transportCancelled()||(guarded&&!sourceReady()))return {false,pro::SnapshotChanged};
  return {match,match?200:(sink.tooLarge()?pro::DigestMismatch:(written<0?written:pro::DigestMismatch))};
}
pro::Result WebDAVClient::checkConnection() {
  auto probe=[this](){
    WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,config_.server_url))return beginError();
    http.addHeader("Depth","0");http.addHeader("Content-Type","application/xml");
    int code=request(http,"PROPFIND","<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/></d:prop></d:propfind>");
    // Do not download directory listings or reflect account information in logs.
    http.end();return code;
  };
  int code=probe();
  if(transportCancelled())return {false,pro::SnapshotChanged};
  // Only create the dedicated standard Koofr backup folder, never a custom URL.
  if(code==404&&config_.server_url==KoofrServer) {
    WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,config_.server_url))return {false,beginError()};
    code=request(http,"MKCOL","");http.end();if(transportCancelled())return {false,pro::SnapshotChanged};if(code!=201&&code!=405)return {false,code};
    code=probe();
  }
  return transportCancelled()?pro::Result{false,pro::SnapshotChanged}:pro::Result{code==207,code};
}
namespace {
constexpr size_t ProbeBodyLimit=4096;
constexpr uint32_t ProbeDeadlineMs=30000;
bool probeSpace(const char* text){return !text||std::strspn(text," \t\r\n")==std::strlen(text);}
bool probeElementSpace(const tinyxml2::XMLElement* element){
  for(auto node=element->FirstChild();node;node=node->NextSibling()){
    if(const auto text=node->ToText()){if(!probeSpace(text->Value()))return false;}
    else if(!node->ToElement())return false;
  }
  return true;
}
int probeStatus(const tinyxml2::XMLElement* status){
  if(!status||status->FirstChildElement()||!status->GetText())return -2011;
  const std::string text=status->GetText();
  if(text.size()<12||(text.compare(0,9,"HTTP/1.1 ")&&text.compare(0,9,"HTTP/1.0 "))||
     (text.size()>12&&text[12]!=' '))return -2011;
  int code=0;for(unsigned i=9;i<12;++i){if(text[i]<'0'||text[i]>'9')return -2011;code=code*10+text[i]-'0';}
  return code;
}
std::string probeTrim(const char* text){
  if(!text)return {};std::string value=text;const auto start=value.find_first_not_of(" \t\r\n");
  if(start==std::string::npos)return {};return value.substr(start,value.find_last_not_of(" \t\r\n")-start+1);
}
bool probeSameResource(const char* href,const String& endpoint){
  std::string value=probeTrim(href),target=endpoint.c_str();
  if(value.empty()||value.size()>256)return false;
  const auto path=target.find('/',8);if(path==std::string::npos)return false;
  std::string targetPath=target.substr(path);
  if(value.size()>1&&value.back()=='/')value.pop_back();
  if(target.size()>1&&target.back()=='/')target.pop_back();
  if(targetPath.size()>1&&targetPath.back()=='/')targetPath.pop_back();
  // Depth:0 must describe exactly our configured collection. Never accept an
  // unrelated resource or another origin merely because its outer status is207.
  return value==target||value==targetPath;
}
int probeCollection(const char* bytes,size_t length,const String& endpoint){
  if(!bytes||!length||length>ProbeBodyLimit||std::memchr(bytes,0,length))return -2011;
  unsigned tags=0;for(size_t i=0;i<length;++i){if(bytes[i]=='<'&&++tags>64)return -2011;if(i+1<length&&bytes[i]=='<'&&bytes[i+1]=='!')return -2011;}
  tinyxml2::XMLDocument doc;if(doc.Parse(bytes,length)!=tinyxml2::XML_SUCCESS)return -2011;
  const auto root=doc.RootElement();if(!pro::davName(root,"multistatus")||root->NextSiblingElement()||!probeElementSpace(root))return -2011;
  const auto response=pro::davChild(root,"response");if(!response)return -2011;
  if(!probeElementSpace(response))return -2011;
  const auto href=pro::davChild(response,"href");
  if(!href||href->FirstChildElement()||!probeSameResource(href->GetText(),endpoint))return -2011;
  const auto direct=pro::davChild(response,"status");
  unsigned properties=0;bool collection=false;int propertyError=-2011;
  for(auto ps=response->FirstChildElement();ps;ps=ps->NextSiblingElement()){
    if(!pro::davName(ps,"propstat"))continue;
    if(direct)return -2011;
    if(!probeElementSpace(ps))return -2011;
    const int status=probeStatus(pro::davChild(ps,"status"));
    const auto prop=pro::davChild(ps,"prop");if(!prop||!probeElementSpace(prop)||status<100||status>599)return -2011;
    for(auto item=prop->FirstChildElement();item;item=item->NextSiblingElement()){
      if(!pro::davName(item,"resourcetype"))continue;
      if(++properties>1)return -2011;
      if(status!=200){propertyError=status>=300?status:-2011;continue;}
      const auto type=pro::davChild(item,"collection");
      collection=type&&!type->NextSiblingElement()&&item->FirstChildElement()==type&&
        !type->FirstChildElement()&&probeElementSpace(type)&&probeElementSpace(item);
    }
  }
  if(direct){const int status=probeStatus(direct);return status==401||status==403||status==404?status:-2011;}
  if(properties!=1)return -2011;
  return collection?207:propertyError;
}
class ReadOnlyProbeSink:public Stream {
 public:
  ReadOnlyProbeSink(char* storage,ResponseBodyGuard& budget):bytes_(storage),budget_(budget){}
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
  size_t write(uint8_t byte)override{return write(&byte,1);}
  size_t write(const uint8_t* data,size_t count)override{
    if(failed_||!budget_.ready()){failed_=true;setWriteError();return 0;}
    if(count>ProbeBodyLimit-size_){overflow_=true;failed_=true;setWriteError();return 0;}
    std::memcpy(bytes_+size_,data,count);size_+=count;budget_.progress(count);return count;
  }
  size_t size()const{return size_;}
  bool cancelled()const{return budget_.error()==pro::SnapshotChanged;}bool expired()const{return budget_.error()==-2012;}bool overflow()const{return overflow_;}
 private:
  char* bytes_;ResponseBodyGuard& budget_;
  size_t size_=0;bool failed_=false,overflow_=false;
};
}
pro::Result WebDAVClient::probeConnectionReadOnly(ProbeCancelled cancel,void* context){
  const auto cancelled=[&](){return cancel&&cancel(context);};
  const uint32_t started=millis();
  const auto expired=[&](){return uint32_t(millis()-started)>=ProbeDeadlineMs;};
  if(cancelled())return {false,pro::SnapshotChanged};
  if(WiFi.status()!=WL_CONNECTED)return {false,-2302};
  if(time(nullptr)<1767225600)return {false,-2303};
  if(!config_.is_valid||!pro::validURL(config_.server_url.c_str())||!config_.ca_pem.length()||
     !config_.username.length()||!config_.password.length())return {false,-2301};
  std::unique_ptr<char[]> body(new(std::nothrow) char[ProbeBodyLimit]);
  if(!body)return {false,-2113};
  size_t length=0;
  {
    ResponseBodyGuard budget(cancel,context,started,ProbeDeadlineMs);ResponseTLS tls;HTTPClient http;
    tls.setCACert(config_.ca_pem.c_str());tls.setHandshakeTimeout(10);tls.setTimeout(15000);
    http.setConnectTimeout(10000);http.setTimeout(15000);http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    // This path deliberately does not call begin(): backup maintenance remains
    // enabled throughout this coordinator-owned read-only check.
    if(!http.begin(tls,config_.server_url)){http.end();return {false,-2001};}
    http.setAuthorization(config_.username.c_str(),config_.password.c_str());
    http.addHeader("Depth","0");http.addHeader("Content-Type","application/xml");
    http.addHeader("Accept-Encoding","identity");
    http.addHeader("Cache-Control","no-cache, no-store");
    if(cancelled()){http.end();return {false,pro::SnapshotChanged};}
    if(expired()){http.end();return {false,-2012};}
    constexpr char Query[]="<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/></d:prop></d:propfind>";
    const int code=http.sendRequest("PROPFIND",Query);
    if(cancelled()){http.end();return {false,pro::SnapshotChanged};}
    if(expired()){http.end();return {false,-2012};}
    if(code!=207){http.end();return {false,code>=200&&code<300?-2011:code};}
    const int declared=http.getSize();
    if(declared==0||declared>int(ProbeBodyLimit)){http.end();return {false,-2011};}
    tls.arm(budget);ReadOnlyProbeSink sink(body.get(),budget);
    const int received=http.writeToStream(&sink);http.end();length=sink.size();
    if(sink.cancelled()||cancelled())return {false,pro::SnapshotChanged};
    if(sink.expired()||expired())return {false,-2012};
    if(sink.overflow()||received<0||size_t(received)!=length||(declared>=0&&size_t(declared)!=length))return {false,-2011};
  }
  // Release all HTTP/TLS scopes before allocating the bounded XML parse tree.
  if(cancelled())return {false,pro::SnapshotChanged};
  const int result=probeCollection(body.get(),length,config_.server_url);
  if(cancelled())return {false,pro::SnapshotChanged};
  if(expired())return {false,-2012};
  return {result==207,result};
}
pro::Result WebDAVClient::testConnection(char* testFile,size_t capacity) {
  char name[72];snprintf(name,sizeof name,"CloudStick-connection-test-%08lx%08lx.txt",(unsigned long)esp_random(),(unsigned long)esp_random());
  if(capacity<=strlen(name))return {false,-2000};
  constexpr char Content[]="Cloud Stick Pro upload and readback test.\n";
  {
    WiFiClientSecure tls;HTTPClient http;if(!begin(http,tls,url(name)))return {false,beginError()};
    http.addHeader("Content-Type","text/plain");http.addHeader("If-None-Match","*");
    int code=request(http,"PUT",reinterpret_cast<uint8_t*>(const_cast<char*>(Content)),sizeof Content-1);http.end();
    if(transportCancelled())return {false,pro::SnapshotChanged};
    if(code!=200&&code!=201&&code!=204)return {false,code};
  }
  snprintf(testFile,capacity,"%s",name);
  ResponseBodyGuard budget(responseCancelled,nullptr,millis(),1800000);ResponseTLS tls;HTTPClient http;if(!begin(http,tls,url(name)))return {false,beginError()};
  http.addHeader("Accept-Encoding","identity");int code=get(http);if(transportCancelled()){http.end();return {false,pro::SnapshotChanged};}if(code!=200){http.end();return {false,code};}
  tls.arm(budget);HashSink sink(sizeof Content-1,budget);int written=http.writeToStream(&sink);
  bool match=written>=0&&sink.matches(hash_text(Content));http.end();
  if(budget.error())return {false,budget.error()};
  if(transportCancelled())return {false,pro::SnapshotChanged};
  return {match,match?200:pro::DigestMismatch};
}
