#pragma once
#include <cstddef>
#include <cstdint>
#ifndef CSP_ENABLE_EVENT_LOG
#define CSP_ENABLE_EVENT_LOG 0
#endif
#if CSP_ENABLE_EVENT_LOG != 0 && CSP_ENABLE_EVENT_LOG != 1
#error CSP_ENABLE_EVENT_LOG must be zero or one
#endif
namespace pro::events {
constexpr size_t Capacity=12,PageCapacity=4,WireRecordBytes=32,WireHeaderBytes=20;
constexpr size_t MaximumSnapshotBytes=WireHeaderBytes+Capacity*WireRecordBytes;
enum class Kind:uint8_t {Boot=1,WorkerStart,ScanOutcome,VerifiedCount,Retry,Error,Retirement,CloudCheck};
// Only numeric, typed facts enter this ring. For VerifiedCount, code is a count
// (0..128), not an error. Other codes are diagnostic results; bytes is a byte
// count, never a filename/hash/config identifier. UTC 0 means clock untrusted.
struct Event {
  uint32_t sequence=0,uptime_ms=0;
  uint64_t utc_unix=0,bytes=0;
  int32_t code=0;
  Kind kind=Kind::Boot;
};
static_assert(sizeof(Event)==32,"Keep the numeric RAM event compact");
struct Status {
  uint32_t newest_sequence=0,persisted_sequence=0;
  uint8_t count=0;
  bool loaded=false,dirty=false,corrupt=false,io_failed=false,sequence_exhausted=false;
};
struct Page {Event events[PageCapacity]{};uint8_t count=0;uint32_t next=0;bool more=false;Status status;};
enum class Load:uint8_t {Disabled,Missing,Loaded,RecoveredTemporary,Corrupt,IOError,AlreadyLoaded};
enum class Flush:uint8_t {Disabled,Unchanged,Saved,IOError};
#if CSP_ENABLE_EVENT_LOG
// record/readPage/status touch RAM only and allocate nothing. Brief critical
// regions protect the ring; storage I/O never runs while holding its lock.
bool record(Kind,int32_t code,uint64_t bytes,uint64_t utc_unix,uint32_t uptime_ms);
Page readPage(uint32_t after_sequence=0);
Status status();
// Current metadata-owning backup worker only. These do not mount/unmount or
// format anything. Call solely outside TLS and after releasing the SD snapshot.
// Repeated starts keep unflushed RAM events instead of overwriting them from disk.
Load workerLoad();
Flush workerFlush(uint32_t uptime_ms=0);
#ifdef CSP_EVENT_LOG_TESTING
void resetForTest();
size_t staticBytesForTest();
#endif
#else
inline bool record(Kind,int32_t,uint64_t,uint64_t,uint32_t){return false;}
inline Page readPage(uint32_t=0){Page page;return page;}
inline Status status(){return {};}
inline Load workerLoad(){return Load::Disabled;}
inline Flush workerFlush(uint32_t=0){return Flush::Disabled;}
#endif
} // namespace pro::events
