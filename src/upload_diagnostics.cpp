#include "upload_diagnostics.h"
#if CSP_UPLOAD_DIAGNOSTICS
#include <Arduino.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <cerrno>
#include <algorithm>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <mutex>
#include <thread>
#endif
namespace {
#ifdef ARDUINO
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
using Task=TaskHandle_t;
Task task(){return xTaskGetCurrentTaskHandle();}
struct Lock {Lock(){portENTER_CRITICAL(&mux);}~Lock(){portEXIT_CRITICAL(&mux);}};
#else
std::mutex mux;
using Task=std::thread::id;
Task task(){return std::this_thread::get_id();}
struct Lock {std::lock_guard<std::mutex> guard{mux};};
#endif
UploadDiagnosticSnapshot state;
Task owner{};
uint32_t started=0,lastPositive=0;
bool admitted(){return state.active&&owner==task();}
void increment(uint32_t& value){if(value!=UINT32_MAX)++value;}
void abort(UploadSourceAbort reason){if(!state.source_abort)state.source_abort=uint8_t(reason);}
bool fatal(int value){return value<0&&value!=MBEDTLS_ERR_SSL_WANT_READ&&value!=MBEDTLS_ERR_SSL_WANT_WRITE;}
}
UploadDiagnosticAttempt::UploadDiagnosticAttempt(uint64_t expected,UploadDiagnosticTarget target){
  const uint32_t now=millis();Lock lock;
  if(state.active)return;
  const uint32_t next=state.attempt==UINT32_MAX?1:state.attempt+1;
  state={};state.enabled=true;state.active=true;state.attempt=attempt_=next;
  state.expected_bytes=expected;state.target=uint8_t(target);owner=task();started=lastPositive=now;
}
void UploadDiagnosticAttempt::finish(int32_t code,uint32_t timeout,bool error,bool eof){
  const uint32_t now=millis();Lock lock;
  if(!attempt_||!admitted()||state.attempt!=attempt_)return;
  state.http_code=code;state.socket_timeout_ms=timeout;
  state.request_ms=now-started;state.last_positive_age_ms=now-lastPositive;
  state.source_ferror=state.source_ferror||error;state.source_eof=state.source_eof||eof;
  if(error)abort(UploadSourceAbort::ReadError);
  else if(eof&&state.source_returned_bytes<state.expected_bytes)abort(UploadSourceAbort::EarlyEof);
  state.completed=true;state.active=false;owner={};attempt_=0;
}
UploadDiagnosticAttempt::~UploadDiagnosticAttempt(){
  const uint32_t now=millis();Lock lock;
  if(attempt_&&admitted()&&state.attempt==attempt_){
    state.request_ms=now-started;state.last_positive_age_ms=now-lastPositive;
    state.active=false;owner={};
  }
}
UploadDiagnosticSnapshot upload_diagnostics_snapshot(){
  Lock lock;const uint32_t now=millis();auto copy=state;copy.enabled=true;
  if(copy.active){copy.request_ms=now-started;copy.last_positive_age_ms=now-lastPositive;}return copy;
}
void upload_diagnostics_source_abort(UploadSourceAbort reason){Lock lock;if(admitted())abort(reason);}
void upload_diagnostics_source_read(size_t requested,size_t got,uint32_t elapsed,int32_t error,bool readError,bool eof,bool returned){
  Lock lock;if(!admitted())return;
  increment(state.source_calls);if(got<requested)increment(state.short_reads);
  state.source_fread_bytes+=got;if(returned)state.source_returned_bytes+=got;
  state.max_read_ms=std::max(state.max_read_ms,elapsed);state.source_ferror=state.source_ferror||readError;state.source_eof=state.source_eof||eof;
  if(readError)state.source_errno=error;
  if(!returned)abort(UploadSourceAbort::Unavailable);
  else if(readError)abort(UploadSourceAbort::ReadError);
  else if(eof&&state.source_returned_bytes<state.expected_bytes)abort(UploadSourceAbort::EarlyEof);
}
extern "C" int __real_mbedtls_ssl_write(mbedtls_ssl_context*,const unsigned char*,size_t);
extern "C" int __real_mbedtls_ssl_read(mbedtls_ssl_context*,unsigned char*,size_t);
extern "C" int __real_mbedtls_net_send(void*,const unsigned char*,size_t);
extern "C" int __wrap_mbedtls_ssl_write(mbedtls_ssl_context* context,const unsigned char* data,size_t size){
  const int incoming=errno;const uint32_t before=millis();errno=incoming;
  const int result=__real_mbedtls_ssl_write(context,data,size);const int saved=errno;
  const uint32_t now=millis();
  {Lock lock;if(admitted()){
    increment(state.tls_write_calls);state.tls_requested_bytes+=size;
    state.tls_last_write_ret=result;state.max_tls_write_ms=std::max(state.max_tls_write_ms,uint32_t(now-before));
    if(result>0){state.tls_accepted_bytes+=unsigned(result);lastPositive=now;}
    if(result==MBEDTLS_ERR_SSL_WANT_READ)increment(state.want_read);
    if(result==MBEDTLS_ERR_SSL_WANT_WRITE)increment(state.want_write);
    if(result<0)state.tls_write_errno=saved;
    if(fatal(result)&&!state.tls_first_fatal_write)state.tls_first_fatal_write=result;
  }}
  errno=saved;return result;
}
extern "C" int __wrap_mbedtls_ssl_read(mbedtls_ssl_context* context,unsigned char* data,size_t size){
  const int result=__real_mbedtls_ssl_read(context,data,size);const int saved=errno;
  {Lock lock;if(admitted()){
    increment(state.tls_read_calls);state.tls_last_read_ret=result;
    if(result<0)state.tls_read_errno=saved;
    if(fatal(result)&&!state.tls_first_fatal_read)state.tls_first_fatal_read=result;
  }}
  errno=saved;return result;
}
extern "C" int __wrap_mbedtls_net_send(void* context,const unsigned char* data,size_t size){
  const int result=__real_mbedtls_net_send(context,data,size);const int saved=errno;
  {Lock lock;if(admitted()){
    increment(state.net_send_calls);state.net_requested_bytes+=size;state.net_last_ret=result;
    if(result>0)state.net_accepted_bytes+=unsigned(result);
    if(result<0){state.net_errno=saved;if(fatal(result)&&!state.net_first_fatal)state.net_first_fatal=result;}
  }}
  errno=saved;return result;
}
#endif
