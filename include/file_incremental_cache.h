#pragma once
#include "pro_core.h"
#include <array>
#include <cstdint>
#include <string>
#ifndef CSP_ENABLE_FILE_INCREMENTAL_CACHE
#define CSP_ENABLE_FILE_INCREMENTAL_CACHE 0
#endif
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE != 0 && CSP_ENABLE_FILE_INCREMENTAL_CACHE != 1
#error "CSP_ENABLE_FILE_INCREMENTAL_CACHE must be boolean"
#endif
namespace pro::incremental {
constexpr size_t RangeLimit=64, SlotCount=128, EntryBytes=192;
constexpr uint32_t AuditMs=24u*60u*60u*1000u;
using Digest=std::array<uint8_t,32>;
struct Stamp { std::array<uint8_t,16> boot{};uint64_t epoch=0,generation=0; };
// Serialized with SnapshotGuard and physical writes by the storage mutex.
// Ranges are never pruned while a proof may reference them. Overflow revokes
// every older proof before coverage starts again; a missed write cannot look clean.
class Coverage {
 public:
  void start(const std::array<uint8_t,16>& boot);
  void invalidate();
  void wrote(uint64_t generation,uint32_t first,uint32_t last);
  void seal(uint64_t generation){sealed_=generation;}
  Stamp stamp(uint64_t generation)const{return {boot_,epoch_,generation};}
  bool clean(const Stamp&,uint64_t current,uint32_t first,uint32_t count)const;
 private:
  struct Range { uint64_t generation;uint32_t first,last; };
  std::array<Range,RangeLimit> ranges_{};
  std::array<uint8_t,16> boot_{};
  uint64_t epoch_=0,last_=0,sealed_=0;size_t used_=0;
};
struct Entry {
  Stamp stamp;uint64_t size=0;uint32_t auditedAt=0;
  Digest scope{},key{},receipt{},layout{};
};
static_assert(sizeof(Coverage)<=1152,"Review incremental coverage RAM budget");
static_assert(sizeof(Entry)<=EntryBytes,"Review incremental cache entry RAM budget");
bool digest(const std::string&,Digest&);
bool encode(const Entry&,uint8_t out[EntryBytes]);
bool decode(const uint8_t in[EntryBytes],Entry&);
bool sameStamp(const Stamp&,const Stamp&);
bool eligible(const Entry&,const Entry& expected,uint32_t now);
// Disposable fixed-slot sidecar with bounded disk lookup and no RAM index. A
// truncated slot/hash never grants authority; full digests are compared.
class Store {
 public:
  explicit Store(std::string path):path_(std::move(path)){}
  bool load(const Digest& key,Entry&)const;
  bool save(const Entry&)const;
 private:std::string path_;
};
}
