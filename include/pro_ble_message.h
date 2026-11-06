#pragma once
#include <cstddef>
#include <cstdint>
#include "ble_feature.h"

namespace pro { namespace ble {
// GATT values are at most 512 bytes, including the Security 2 16-byte GCM tag.
constexpr size_t MessageLimit = CSP_ENABLE_WIFI_SETUP ? 496 : 96;
constexpr size_t ResponseLimit = CSP_ENABLE_WIFI_SETUP ? 496 : 384;
// Timers are bounded to minutes, far below the uint32 half range. Modular
// subtraction handles ordinary millis() wrap; a newer/future sample is not an
// elapsed interval. The adapter also guards decisions by state generation.
inline bool elapsedAtLeast(uint32_t now,uint32_t then,uint32_t interval) {
  uint32_t elapsed=now-then;
  return elapsed<0x80000000U && elapsed>=interval;
}
constexpr size_t GcmTagSize = 16;
constexpr size_t HandshakeLimit = 512;
constexpr uint32_t MaxRequestId = 0x7fffffff;
enum class Operation { Info, Status };
struct Request { uint32_t id = 0; Operation operation = Operation::Info; };

// Validate before entering the SDK decrypt path: IDF 5.5.5 subtracts the tag
// length before allocating. This also caps allocation for unauthenticated input.
inline bool encryptedFrameSize(size_t n) {
  return n > GcmTagSize && n <= MessageLimit + GcmTagSize;
}
inline bool handshakeFrameSize(size_t n) { return n > 0 && n <= HandshakeLimit; }

// The tiny read-only application grammar is intentionally narrower than general
// JSON: ordered v/id/op fields, no escapes/duplicates/extra fields, bounded IDs.
// Phone JSON.stringify produces this grammar. Cryptographic framing is entirely
// Espressif Security 2; this parser only validates decrypted application data.
inline bool parseRequest(const uint8_t* bytes, size_t size, Request& request) {
  if (!bytes || !size || size > MessageLimit) return false;
  size_t at = 0;
  auto ws = [&]() { while (at < size && (bytes[at]==' ' || bytes[at]=='\t' || bytes[at]=='\n' || bytes[at]=='\r')) ++at; };
  auto take = [&](const char* literal) {
    ws(); size_t next = at;
    for (size_t i=0; literal[i]; ++i,++next) if (next>=size || bytes[next]!=uint8_t(literal[i])) return false;
    at=next; return true;
  };
  if (!take("{") || !take("\"v\"") || !take(":") || !take("1") || !take(",") ||
      !take("\"id\"") || !take(":")) return false;
  ws();
  if (at>=size || bytes[at]<'1' || bytes[at]>'9') return false;
  uint32_t id=0;
  while (at<size && bytes[at]>='0' && bytes[at]<='9') {
    uint32_t digit=bytes[at++]-'0';
    if (id>(MaxRequestId-digit)/10) return false;
    id=id*10+digit;
  }
  if (!take(",") || !take("\"op\"") || !take(":")) return false;
  Operation op;
  if (take("\"info\"")) op=Operation::Info;
  else if (take("\"status\"")) op=Operation::Status;
  else return false;
  if (!take("}")) return false;
  ws(); if (at!=size) return false;
  request={id,op}; return true;
}

class RequestSequence {
  uint32_t session_=0,last_=0;
  bool opened_=false;
 public:
  bool open(uint32_t session) { if(opened_) return false; session_=session;last_=0;opened_=true;return true; }
  void close() { opened_=false;session_=0;last_=0; }
  bool accept(uint32_t session,uint32_t id) {
    if(!opened_ || session!=session_ || !id || id>MaxRequestId || id<=last_) return false;
    last_=id;return true;
  }
};
} }
