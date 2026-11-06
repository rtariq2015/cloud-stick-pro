// Separate translation unit is deliberate: actual SDK calls must traverse the
// linker wrappers; a same-TU fake could inline and accidentally bypass them.
#include "stubs/transport_fixture.h"
#include <algorithm>
#include <cerrno>
#include <climits>
extern "C" int mbedtls_ssl_write(mbedtls_ssl_context* context,const unsigned char* bytes,size_t size){
  if(transientCount){--transientCount;errno=EAGAIN;return transientCount%2?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_SSL_WANT_WRITE;}
  if(headerSent&&payloadAccepted>=failAfter){errno=ECONNRESET;return failResult;}
  const size_t n=std::min(size,recordLimit);const int sent=mbedtls_net_send(context,bytes,n);if(sent<0)return sent;
  accepted.insert(accepted.end(),bytes,bytes+n);allAccepted+=n;if(headerSent)payloadAccepted+=n;
  errno=29;return int(n);
}
extern "C" int mbedtls_ssl_read(mbedtls_ssl_context*,unsigned char*,size_t){errno=ECONNRESET;return readResult;}
