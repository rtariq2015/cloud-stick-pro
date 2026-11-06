#pragma once
#include <Arduino.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <vector>
extern size_t payloadAccepted,allAccepted,failAfter,recordLimit,netFailAfter;
extern int failResult,readResult,netResult;
extern unsigned transientCount;
extern bool headerSent;
extern std::vector<uint8_t> accepted;
