#pragma once
#include "wifi_setup_model.h"

namespace pro::wifi_setup {
constexpr size_t NetworkRecordSize = 860, SelectionSize = 24;
enum class Read { Found, Missing, Error };
// Separate namespace adapter. read reports exact length; write commits one key.
// No erase/format or other namespace operations are available to this interface.
class BlobStore {
 public:
  virtual ~BlobStore() = default;
  virtual Read read(const char* key, uint8_t* out, size_t capacity, size_t& length) = 0;
  virtual bool write(const char* key, const uint8_t* bytes, size_t length) = 0;
};
enum class Stored { Valid, Missing, Corrupt, IO };
enum class Published { Committed, Unchanged, Cancelled, Conflict, Unknown };
using StillAuthorized = bool (*)(void*);
bool encodeNetworks(const StoredNetworks&, uint8_t out[NetworkRecordSize]);
bool decodeNetworks(const uint8_t*, size_t, StoredNetworks&);
class ProfileStore {
 public:
  explicit ProfileStore(BlobStore& blobs) : blobs_(blobs) {}
  ~ProfileStore();
  Stored load(StoredNetworks&);
  Published publish(const StoredNetworks&, uint64_t expected_revision,
                    StillAuthorized, void* context);
 private:
  BlobStore& blobs_;
  // Worker-owned bounded scratch; never allocated on a GATT or event stack.
  std::array<uint8_t, NetworkRecordSize> image_{}, verify_{};
  std::array<uint8_t, SelectionSize> marker_{};
  Stored loadCurrent(StoredNetworks&, uint8_t& selected);
};
} // namespace pro::wifi_setup
