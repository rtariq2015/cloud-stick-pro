#include "cloud_request_trace.h"
#include <mbedtls/ssl.h>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <thread>
std::atomic<uint32_t> traceNow{100};
int fakeTlsResult=0,calls=0;
mbedtls_ssl_context* expectedContext=nullptr;
unsigned char* expectedBytes=nullptr;
size_t expectedLength=0;
void fakeTlsCall(mbedtls_ssl_context* context,unsigned char* bytes,size_t length){
 assert(context==expectedContext&&bytes==expectedBytes&&length==expectedLength);++calls;
}
int main(){
 mbedtls_ssl_context context;unsigned char bytes[4];
 for(int result:{2,0,MBEDTLS_ERR_SSL_WANT_READ,MBEDTLS_ERR_SSL_WANT_WRITE,-0x7880,-0x7180}){
  for(auto& b:bytes)b=0xa5;
  CloudRequestTrace event(CloudRequestOp::FinalReadback);
  expectedContext=&context;expectedBytes=bytes;expectedLength=2;fakeTlsResult=result;calls=0;errno=0;
  assert(mbedtls_ssl_read(&context,bytes,2)==result&&calls==1&&errno==EPIPE);
  assert(bytes[2]==0xa5&&bytes[3]==0xa5);
  assert(bytes[0]==(result>0?0x5a:0xa5));
  const int recorded=result<0&&result!=MBEDTLS_ERR_SSL_WANT_READ&&result!=MBEDTLS_ERR_SSL_WANT_WRITE?result:0;
  assert(cloud_request_snapshot().current.tls_error==recorded);
  auto outcome=event.finish({false,-4});assert(!outcome.ok&&outcome.code==-4);
  assert(cloud_request_snapshot().last.tls_error==recorded);
 }
 {
  CloudRequestTrace event(CloudRequestOp::StageReadback);
  expectedContext=&context;expectedBytes=nullptr;expectedLength=0;fakeTlsResult=-0x7180;calls=0;
  std::thread other([&]{assert(mbedtls_ssl_read(&context,nullptr,0)==-0x7180);});other.join();
  assert(calls==1&&cloud_request_snapshot().current.tls_error==0);
  assert(mbedtls_ssl_read(&context,nullptr,0)==-0x7180);
  event.finish({false,-4});assert(cloud_request_snapshot().last.tls_error==-0x7180);
 }
 const auto prior=cloud_request_snapshot().last_negative;
 assert(mbedtls_ssl_read(&context,nullptr,0)==-0x7180);
 assert(cloud_request_snapshot().last_negative.id==prior.id&&cloud_request_snapshot().last_negative.tls_error==prior.tls_error);
 std::cout<<"PASS real linker interception, exact TLS argument/result/errno forwarding, no payload inspection, WANT exclusion and request-task isolation\n";
}
