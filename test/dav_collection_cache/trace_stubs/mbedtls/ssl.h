#pragma once
#include <cstddef>
// Synthetic TLS ABI only; these two statuses match the retained SDK header.
#define MBEDTLS_ERR_SSL_WANT_READ -0x6900
#define MBEDTLS_ERR_SSL_WANT_WRITE -0x6880
struct mbedtls_ssl_context {int fixture=0;};
extern "C" int mbedtls_ssl_read(mbedtls_ssl_context*,unsigned char*,size_t);
