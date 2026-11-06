#pragma once
#include <cstddef>
constexpr int MBEDTLS_ERR_NET_CONN_RESET=-0x0050,MBEDTLS_ERR_NET_SEND_FAILED=-0x004e;
extern "C" int mbedtls_net_send(void*,const unsigned char*,size_t);
