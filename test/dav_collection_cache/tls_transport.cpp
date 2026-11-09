#include <mbedtls/ssl.h>
#include <cerrno>
extern int fakeTlsResult;
void fakeTlsCall(mbedtls_ssl_context*,unsigned char*,size_t);
extern "C" int mbedtls_ssl_read(mbedtls_ssl_context* context,unsigned char* bytes,size_t length){
  fakeTlsCall(context,bytes,length);errno=EPIPE;
  for(int i=0;i<fakeTlsResult&&size_t(i)<length;++i)bytes[i]=0x5a;
  return fakeTlsResult;
}
