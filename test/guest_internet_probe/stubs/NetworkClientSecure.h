#pragma once
#include "NetworkClient.h"
class NetworkClientSecure:public NetworkClient {
public:
 NetworkClientSecure(){secure=true;}
 void useBuiltinCACertBundle(){bundle=true;}
 void setHandshakeTimeout(unsigned long seconds){handshakeTimeout=seconds;}
 void setInsecure(){insecure=true;}
 int lastError(char* buffer,size_t length){if(buffer&&length)buffer[0]=0;return plan.tlsError;}
};
