#pragma once
#include "ble_feature.h"
#include <cstddef>
#include <cstdint>
#include <string>
#ifndef CSP_ENABLE_FILE_HISTORY
#define CSP_ENABLE_FILE_HISTORY 0
#endif
#if CSP_ENABLE_FILE_HISTORY != 0 && CSP_ENABLE_FILE_HISTORY != 1
#error CSP_ENABLE_FILE_HISTORY must be zero or one
#endif
#if CSP_ENABLE_FILE_HISTORY && !CSP_ENABLE_BACKUP_SETUP
#error File history requires the owner-authenticated combined backup build
#endif
#if CSP_ENABLE_FILE_HISTORY
namespace pro {class FileJournal;}
#endif
namespace pro::history {
constexpr size_t Capacity=8,FragmentBytes=64;
struct Status {
  uint64_t captured_unix=0;
  uint32_t revision=0;
  uint16_t matched=0;
  uint8_t count=0;
  bool loaded=false,complete=false,limited=false,corrupt=false,io_failed=false,allocation_failed=false,exhausted=false;
};
struct Fragment {
  uint64_t bytes=0,verified=0,retry=0;
  uint32_t revision=0,attempts=0;
  int32_t error=0;
  uint16_t path_bytes=0,offset=0,next=0;
  uint8_t index=0,count=0,state=0;
  bool more=false;
  char hex[FragmentBytes*2+1]{};
};
enum class Read {Found,Unavailable,Stale,Invalid};
#if CSP_ENABLE_FILE_HISTORY
// Worker retirement only: every engine/remote/TLS scope must already be gone,
// while this worker still exclusively owns the metadata mount. Reads receipts;
// never saves them, touches SD, or mounts/formats a filesystem.
bool capture(FileJournal&,const std::string& destination,const std::string& folder,uint64_t utc);
// Owner calls before admitting any new worker/TLS. Detach under a short lock,
// then erase/free the entire allocated block outside the critical section.
void release();
Status status();
// RAM only. Caller must separately prove the private owner session, maintenance
// and worker Stopped; revision/index/offset are checked again here.
Read read(uint32_t revision,uint32_t index,uint32_t offset,Fragment&);
#ifdef CSP_FILE_HISTORY_TESTING
size_t residentBytesForTest();
size_t cacheBytesForTest();
bool seedRevisionForTest(uint32_t);
#endif
#else
inline void release(){}
inline Status status(){Status s;return s;}
inline Read read(uint32_t,uint32_t,uint32_t,Fragment& out){out=Fragment{};return Read::Unavailable;}
#endif
}
