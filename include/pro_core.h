#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace pro {
constexpr size_t PathLimit = 240;
constexpr size_t FileLimit = 128;
constexpr size_t ChunkSize = 2048;
constexpr size_t RecordSize = 512;
enum class Owner : uint8_t { Host, Ejected, Local, Fault };
bool canRestart(Owner owner,bool backupActive);
// Caller serializes transitions and raw I/O under the same mutex.
class Ownership {
 public:
  Owner state() const { return owner_; }
  bool hostIO() const { return owner_ == Owner::Host; }
  bool eject() { if (!hostIO()) return false; owner_ = Owner::Ejected; return true; }
  bool mounted(bool ok) {
    if (owner_ != Owner::Ejected) return false;
    owner_ = ok ? Owner::Local : Owner::Fault; return ok;
  }
  void fault() { owner_ = Owner::Fault; }
 private:
  Owner owner_ = Owner::Host;
};
bool validPath(const std::string& path);
std::string encodePath(const std::string& path);
bool validURL(const std::string& url);
bool validWiFi(const std::string& ssid,const std::string& password,bool open);
bool validHex(const char* text);
bool validRange(uint64_t sectors, uint32_t lba, uint32_t offset, uint32_t size);
uint32_t crc32(const uint8_t* data, size_t size);
uint32_t retrySeconds(uint32_t attempts);
enum class State : uint8_t { Pending = 0, Transferred = 1, Verified = 2 };
struct Record {
  std::string path, digest, destination;
  uint64_t size = 0, next = 0, verifiedAt = 0;
  uint32_t sequence = 0, attempts = 0;
  int32_t error = 0;
  State state = State::Pending;
};
bool encodeRecord(const Record& r, uint8_t out[RecordSize]);
bool decodeRecord(const uint8_t in[RecordSize], Record& r);
enum class Load { Missing, Found, Corrupt, IOError };
struct Result { bool ok; int32_t code; };
constexpr int32_t DigestMismatch = -2002;
constexpr int32_t SnapshotChanged = -2010;
class Local {
 public:
  virtual ~Local() = default;
  virtual bool fingerprint(const std::string&, std::string&, uint64_t&) = 0;
  virtual std::string key(const std::string&) = 0;
  // Optional, disposable local-layout proofs. Backup passes only the exact
  // current destination/path's decoded Verified receipt, never a size/mtime hint.
  virtual bool reuseVerified(const Record&){return false;}
  virtual void rememberVerified(const Record&){}
};
class Journal {
 public:
  virtual ~Journal() = default;
  virtual Load load(const std::string&, Record&) = 0;
  virtual bool save(const std::string&, const Record&) = 0;
};
class Remote {
 public:
  virtual ~Remote() = default;
  virtual Result put(const std::string&, uint64_t) = 0;
  virtual Result verify(const std::string&, const std::string&, uint64_t) = 0;
};
enum class Outcome { Pending, Transferred, Verified, Failed, MetadataError, LocalError, Invalid };
struct Step { Outcome outcome; int32_t error = 0; bool transferred = false; bool repaired = false; uint64_t verifiedAt = 0; };
class Backup {
 public:
  Backup(Local& l, Journal& j, Remote& r, const std::string& destination,uint64_t (*clock)()=nullptr)
    : local_(l), journal_(j), remote_(r), destination_(destination),clock_(clock) {}
  Step process(const std::string& path, uint64_t now);
 private:
  Local& local_; Journal& journal_; Remote& remote_; std::string destination_;uint64_t (*clock_)();
  bool persist(const std::string&, Record&);
};
}
