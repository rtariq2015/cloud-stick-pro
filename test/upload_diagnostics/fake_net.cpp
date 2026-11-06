#include "stubs/transport_fixture.h"
#include <cerrno>
#include <climits>
extern "C" int mbedtls_net_send(void*,const unsigned char*,size_t size){
  if(netResult!=INT32_MAX&&payloadAccepted>=netFailAfter){errno=EPIPE;return netResult;}errno=17;return int(size);
}
