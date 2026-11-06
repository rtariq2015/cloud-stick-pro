// Real production WebDAV/FileStream/diagnostics plus byte-for-byte extracted
// pinned HTTP stream, secure write and TLS send-loop implementations. Only
// mbedTLS return values, SDK/task APIs and remote replies are synthetic.
#include "webdav_client.h"
#include "upload_diagnostics.h"
#include "pro_device.h"
#include "usb_storage.h"
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <sys/time.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>
std::atomic<uint32_t> fakeNow{0};
unsigned liveTLS=0,liveHTTP=0;
std::deque<FakeReply> fakeReplies;
std::vector<std::string> sentMethods;
std::function<void()> beforePayload,afterFread;
bool paused=false,shortSource=false,brokenSource=false;
size_t payloadAccepted=0,allAccepted=0,failAfter=SIZE_MAX,recordLimit=4096,netFailAfter=0;
int failResult=0,readResult=MBEDTLS_ERR_SSL_WANT_READ,netResult=INT32_MAX;
unsigned transientCount=0,passed=0;
bool headerSent=false;
const std::string Payload(8193,'Q');
std::vector<uint8_t> accepted;
int handle_error(int value){return value;}
#include "actual_send_ssl_data.inc"
#include "actual_secure_write.inc"
#include "actual_http_stream.inc"
bool maintenance_active(){return paused;}
bool storage_ready(){return true;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){}
void backup_progress(uint64_t,uint64_t){}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
  assert(std::string(path)=="/sdcard/QA/synthetic.pdf"&&std::string(mode)=="rb");
  FILE* file=std::tmpfile();assert(file);const size_t n=shortSource?Payload.size()-100:Payload.size();
  assert(std::fwrite(Payload.data(),1,n,file)==n);std::rewind(file);
  if(brokenSource)assert(!close(fileno(file)));return file;
}
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" size_t __wrap_fread(void* output,size_t size,size_t count,FILE* file){
  const size_t result=__real_fread(output,size,count,file);if(afterFread)afterFread();return result;
}
CloudConfig config(bool koofr=false){
  CloudConfig c;c.is_valid=true;c.server_url=koofr?"https://app.koofr.net/dav/Koofr/Synthetic/":"https://synthetic.invalid/";
  c.username="fixture-user";c.password="SYNTHETIC_SECRET_NEVER_LOG";c.ca_pem="fixture-ca";return c;
}
void reset(){
  assert(!liveTLS&&!liveHTTP);fakeReplies.clear();sentMethods.clear();beforePayload={};afterFread={};
  paused=shortSource=brokenSource=false;payloadAccepted=allAccepted=0;failAfter=SIZE_MAX;
  recordLimit=4096;failResult=0;transientCount=0;headerSent=false;accepted.clear();netResult=INT32_MAX;netFailAfter=0;
  beforePayload=[](){headerSent=true;};
}
void pass(const char* name){assert(!liveTLS&&!liveHTTP);++passed;std::cout<<"PASS "<<name<<'\n';}
pro::Result put(){
  auto c=config();WebDAVClient remote(c);fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
  return remote.put("QA/synthetic.pdf",Payload.size());
}
int main(){
  for(size_t limit:{size_t(4096),size_t(2048),size_t(1024)}){
    reset();recordLimit=limit;auto result=put();assert(result.ok&&result.code==201);
    auto s=upload_diagnostics_snapshot();assert(s.enabled&&s.completed&&!s.active&&s.source_abort==0);
    assert(s.expected_bytes==Payload.size()&&s.source_fread_bytes==Payload.size()&&s.source_returned_bytes==Payload.size());
    assert(s.socket_timeout_ms==10000&&s.http_code==201&&s.tls_accepted_bytes==Payload.size()+4);
    assert(accepted.size()==Payload.size()+4&&std::memcmp(accepted.data()+4,Payload.data(),Payload.size())==0);
    pass("positive partial records preserve source and exact TLS bytes");
  }
  for(int failure:{0,MBEDTLS_ERR_SSL_WANT_WRITE,MBEDTLS_ERR_SSL_WANT_READ,MBEDTLS_ERR_NET_CONN_RESET,MBEDTLS_ERR_NET_SEND_FAILED}){
    reset();failAfter=1460;failResult=failure;auto result=put();assert(!result.ok&&result.code==-3);
    const auto s=upload_diagnostics_snapshot();assert(s.completed&&s.http_code==-3&&s.source_abort==0);
    assert(s.tls_accepted_bytes>=1460+4&&s.source_returned_bytes>s.tls_accepted_bytes-4&&s.source_returned_bytes<Payload.size());
    const bool waiting=failure==MBEDTLS_ERR_SSL_WANT_WRITE||failure==MBEDTLS_ERR_SSL_WANT_READ;
    assert(s.tls_first_fatal_write==(waiting?0:failure)&&s.tls_last_write_ret==failure);
    if(!failure||waiting)assert(s.request_ms>10000&&s.last_positive_age_ms>10000&&s.max_tls_write_ms==0);
    if(failure==MBEDTLS_ERR_SSL_WANT_WRITE)assert(s.want_write>0);
    if(failure==MBEDTLS_ERR_SSL_WANT_READ)assert(s.want_read>0);
    pass(waiting?"WANT stall after positive bytes remains an observed timeout without false fatal":failure?"fatal TLS result after positive bytes stays failed and numeric":"zero-progress timeout after positive bytes stays failed and numeric");
  }
  reset();shortSource=true;auto result=put();assert(!result.ok&&result.code==-3);
  auto s=upload_diagnostics_snapshot();assert(s.source_abort==uint8_t(UploadSourceAbort::EarlyEof)&&s.source_eof&&!s.source_ferror);
  assert(s.short_reads==1&&s.source_fread_bytes==Payload.size()-100&&s.source_returned_bytes==Payload.size()-100);
  pass("actual FileStream early EOF is distinguishable from TLS failure without false success");
  reset();brokenSource=true;result=put();assert(!result.ok&&result.code==-2003);
  s=upload_diagnostics_snapshot();assert(s.source_abort==uint8_t(UploadSourceAbort::ReadError)&&s.source_ferror&&s.source_errno==EBADF);
  pass("actual fread error/errno is preserved independently of HTTP error");
  reset();beforePayload=[](){headerSent=true;paused=true;};result=put();assert(!result.ok&&result.code==pro::SnapshotChanged);
  s=upload_diagnostics_snapshot();assert(s.source_abort==uint8_t(UploadSourceAbort::Unavailable)&&s.source_returned_bytes==0);
  pass("maintenance before first source read retains snapshot cancellation result");
  reset();afterFread=[](){paused=true;};result=put();assert(!result.ok&&result.code==pro::SnapshotChanged);
  s=upload_diagnostics_snapshot();assert(s.source_abort==uint8_t(UploadSourceAbort::Unavailable)&&s.source_fread_bytes>0&&s.source_returned_bytes==0);
  pass("maintenance after fread does not publish unread source bytes as returned");
  reset();beforePayload=[](){headerSent=true;fakeNow+=1800001;};result=put();assert(!result.ok&&result.code==-2012);
  s=upload_diagnostics_snapshot();assert(s.source_abort==uint8_t(UploadSourceAbort::Deadline)&&s.request_ms>1800000);
  pass("unchanged 30-minute source deadline is numeric and remains enforced");
  reset();netResult=MBEDTLS_ERR_NET_CONN_RESET;result=put();assert(!result.ok&&result.code==-2);
  s=upload_diagnostics_snapshot();assert(s.net_first_fatal==MBEDTLS_ERR_NET_CONN_RESET&&s.net_errno==EPIPE);
  pass("network reset code and errno are captured before secure client disposal");
  reset();netResult=MBEDTLS_ERR_NET_CONN_RESET;netFailAfter=1460;result=put();assert(!result.ok&&result.code==-3);
  s=upload_diagnostics_snapshot();assert(s.net_accepted_bytes>=1464&&s.net_first_fatal==MBEDTLS_ERR_NET_CONN_RESET&&s.net_errno==EPIPE);
  assert(s.tls_first_fatal_write==MBEDTLS_ERR_NET_CONN_RESET&&s.source_abort==0);
  pass("net-send reset after positive source bytes retains original HTTP failure and both numeric layers");
  reset();
  {
    UploadDiagnosticAttempt scope(100,UploadDiagnosticTarget::Replacement);mbedtls_ssl_context context;
    transientCount=2;errno=7;uint8_t byte=1;
    assert(mbedtls_ssl_write(&context,&byte,1)==MBEDTLS_ERR_SSL_WANT_READ&&errno==EAGAIN);
    assert(mbedtls_ssl_write(&context,&byte,1)==MBEDTLS_ERR_SSL_WANT_WRITE&&errno==EAGAIN);
    readResult=MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    assert(mbedtls_ssl_read(&context,&byte,0)==readResult&&errno==ECONNRESET);
    const auto before=upload_diagnostics_snapshot();
    std::thread other([&]{mbedtls_ssl_context foreign;mbedtls_ssl_read(&foreign,&byte,0);});other.join();
    assert(upload_diagnostics_snapshot().tls_read_calls==before.tls_read_calls);
    scope.finish(-3,10000,false,false);
  }
  s=upload_diagnostics_snapshot();assert(s.want_read==1&&s.want_write==1&&s.tls_first_fatal_read==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY);
  assert(s.target==uint8_t(UploadDiagnosticTarget::Replacement));
  pass("wrappers preserve real returns/errno and exclude other tasks");
  const auto final=upload_diagnostics_snapshot();
  {mbedtls_ssl_context context;uint8_t byte=0;mbedtls_ssl_read(&context,&byte,0);}
  assert(upload_diagnostics_snapshot().tls_read_calls==final.tls_read_calls);
  pass("completed snapshot remains fixed outside an upload scope");
  fakeNow=UINT32_MAX-120000u;
  std::atomic<bool> finished{false},invalidAge{false};
  std::thread observer([&]{while(!finished.load()){
    const auto current=upload_diagnostics_snapshot();
    if(current.active&&(current.request_ms>=0x80000000u||current.last_positive_age_ms>=0x80000000u))invalidAge=true;
  }});
  for(unsigned i=0;i<2000;++i){
    fakeNow+=60000;UploadDiagnosticAttempt scope(1,UploadDiagnosticTarget::Stage);
    scope.finish(201,10000,false,false);
  }
  finished=true;observer.join();assert(!invalidAge.load());
  pass("concurrent owner snapshot timestamps stay coherent across new attempts and clock wrap");
  std::cout<<passed<<" actual adapter/HTTP/secure/TLS diagnostic case groups passed; snapshot_bytes="<<sizeof(UploadDiagnosticSnapshot)<<'\n';
}
