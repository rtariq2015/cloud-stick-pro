#include "cloud_request_trace.h"
#if CSP_ENABLE_CLOUD_TLS_TRACE
#include <mbedtls/ssl.h>
#include <cerrno>

extern "C" int __real_mbedtls_ssl_read(mbedtls_ssl_context*,unsigned char*,size_t);
// Observe the existing reader's return value. Forward exactly once with the
// original context/buffer/length; never inspect or retain any pointed-to data.
// No retry, free, socket/crypto setting, result translation or errno change.
extern "C" int __wrap_mbedtls_ssl_read(mbedtls_ssl_context* context,unsigned char* bytes,size_t length){
  const int result=__real_mbedtls_ssl_read(context,bytes,length);
  if(result<0&&result!=MBEDTLS_ERR_SSL_WANT_READ&&result!=MBEDTLS_ERR_SSL_WANT_WRITE){
    const int saved=errno;cloud_request_tls_error(result);errno=saved;
  }
  return result;
}
#endif
