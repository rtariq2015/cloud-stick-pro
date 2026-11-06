#pragma once
#include <cstddef>
struct mbedtls_ssl_context {int unused=0;};
constexpr int MBEDTLS_ERR_SSL_WANT_READ=-0x6900,MBEDTLS_ERR_SSL_WANT_WRITE=-0x6880;
constexpr int MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY=-0x7880;
extern "C" int mbedtls_ssl_write(mbedtls_ssl_context*,const unsigned char*,size_t);
extern "C" int mbedtls_ssl_read(mbedtls_ssl_context*,unsigned char*,size_t);
