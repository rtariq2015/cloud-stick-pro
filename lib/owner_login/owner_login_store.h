#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <owner_record.h>
#include <wifi_profile_store.h>

namespace pro::owner_login {
constexpr size_t RecordSize=pro::wifi_setup::OwnerRecordSize;
static_assert(RecordSize==432,"Owner-login updates preserve the COW2 layout");
struct Bytes {
  std::array<uint8_t,RecordSize> value{};
  ~Bytes();
};
struct Current {Bytes bytes;uint64_t revision=0;};
enum class Loaded:uint8_t {Ready,Missing,Corrupt,Unavailable};
enum class Saved:uint8_t {Committed,Conflict,Cancelled,Invalid,Unavailable,Unknown};
using BeforeWrite=bool (*)(void*);
// SHA-256's first eight digest bytes, big-endian, with successful zero mapped
// to1. Zero is reserved for a hashing-library failure, never a valid revision.
uint64_t fingerprint(const Bytes&);
class Store {
 public:
  Store(pro::wifi_setup::BlobStore&,const uint8_t mac[6]);
  Loaded load(Current&);
  Saved publish(const Current&,const Bytes&,uint64_t expected,
    pro::wifi_setup::StillAuthorized,void* auth_context,uint64_t& new_revision,
    BeforeWrite before_write=nullptr,void* before_context=nullptr);
 private:
  pro::wifi_setup::BlobStore& blobs_;
  std::array<uint8_t,6> mac_{};
  bool identity_=false;
};
} // namespace pro::owner_login
