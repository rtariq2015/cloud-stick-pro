#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Source-only Milestone 2 policy. No radio, NVS, filesystem, crypto or reset API.
// One owner task serializes calls. Access comes from a trusted session adapter,
// never from phone JSON. The public M1 test proof is deliberately insufficient.
namespace pro::wifi_setup {
constexpr size_t ProfileLimit = 8;
constexpr uint32_t CandidateLifetimeMs = 120000;
enum class Proof : uint8_t { None, LegacyShared, SyntheticM1, DeviceOwner };
struct Access {
  uint64_t session = 0;
  Proof proof = Proof::None;
  bool physical_window = false;
  bool terminal_rejection = false;
  bool workers_quiescent = false;
};
enum class Security : uint8_t { Open, Wpa2Personal };
enum class SecretAction : uint8_t { Keep, Replace, Forget };
enum class Error : uint8_t {
  None, Denied, Busy, StaleRevision, StaleTransaction, Expired, Invalid,
  Full, Missing, ReplaceSecret, ConfirmationRequired, CheckRequired,
  WorkersBusy, Exhausted, WrongPassword, Unavailable, Dhcp, Dns,
  Captive, EndpointUnreachable, LocalOnly
};
enum class Phase : uint8_t { Idle, Editing, Checking, Ready, Failed };
struct Edit {
  uint32_t id = 0; // zero allocates a stable ID; existing IDs are never reused
  std::string_view ssid;
  Security security = Security::Wpa2Personal;
  SecretAction secret_action = SecretAction::Replace;
  std::string_view secret;
  uint8_t priority = 0; // lower comes first
  bool enabled = true;
};
struct ProfileView {
  uint32_t id = 0;
  std::array<char, 33> ssid{};
  Security security = Security::Wpa2Personal;
  uint8_t priority = 0;
  bool enabled = false;
  bool has_secret = false;
};
struct Snapshot {
  uint64_t revision = 0, transaction = 0;
  uint32_t edit = 0;
  Phase phase = Phase::Idle;
  Error check_error = Error::None;
  size_t count = 0;
  bool durable = false; // this module never saves to persistent storage
  std::array<ProfileView, ProfileLimit> profiles{};
};
struct Ticket {
  uint64_t session = 0, transaction = 0;
  uint32_t edit = 0, profile = 0, attempt = 0;
};
struct CheckProfile {
  uint32_t id;
  std::string_view ssid, secret;
  Security security;
};
// Internal persistence representation. Never serialize this type to a phone,
// diagnostic stream or log: it contains passwords. Codec/store own their bounds.
struct StoredProfile {
  uint32_t id = 0;
  std::array<char, 33> ssid{};
  std::array<char, 64> secret{};
  Security security = Security::Wpa2Personal;
  uint8_t priority = 0;
  bool enabled = true;
};
struct StoredNetworks {
  uint64_t revision = 1;
  uint32_t next_id = 1, selected = 0;
  size_t count = 0;
  std::array<StoredProfile, ProfileLimit> profiles{};
};
bool validStored(const StoredNetworks&);
void eraseSecretMemory(void*, size_t);
using CheckVisitor = void (*)(void*, const CheckProfile&);

class Candidate {
 public:
  Candidate() = default;
  ~Candidate();
  Candidate(const Candidate&) = delete;
  Candidate& operator=(const Candidate&) = delete;
  // All mutation requires a verified device owner AND physical setup window.
  Error begin(const Access&, uint64_t expected_revision, uint32_t now,
              uint64_t& transaction);
  Error stage(const Access&, uint64_t transaction, uint32_t now, const Edit&,
              uint32_t& assigned_id);
  Error forget(const Access&, uint64_t transaction, uint32_t now, uint32_t id,
               bool confirm_last_enabled);
  Error startCheck(const Access&, uint64_t transaction, uint32_t now,
                   uint32_t profile, Ticket&);
  // Internal worker-only borrow, never a transport response. The visitor must
  // not retain these views, reenter the model, or log/serialize the secret.
  Error borrowCheckProfile(const Access&, const Ticket&, uint32_t now,
                           CheckVisitor, void* context);
  // A result is from the trusted worker, not the phone. None means the exact
  // candidate passed association/DHCP/DNS AND the approved endpoint probe.
  // Association alone or a phone-side portal login must never supply None.
  Error finishCheck(const Access&, const Ticket&, uint32_t now, Error result);
  Error commitInMemory(const Access&, uint64_t transaction, uint32_t edit,
                       uint64_t expected_revision, uint32_t now);
  // A local-only Wi-Fi test proves association/address, never internet/cloud.
  // Its separate explicit-save path must label that limit to the owner.
  Error preparePersistent(const Access&, uint64_t transaction, uint32_t edit,
                          uint64_t expected_revision, uint32_t now,
                          bool accept_local_only, StoredNetworks&);
  // Trusted boot/store adapter only; not a management command. Import validates
  // the entire record, clears any candidate and restores stable IDs/revision.
  bool loadTrusted(const StoredNetworks&);
  bool exportActive(StoredNetworks&) const;
  Error abort(const Access&, uint64_t transaction, uint32_t now);
  Error snapshot(const Access&, uint32_t now, bool candidate, Snapshot&);
  void closeSession(uint64_t session);
  void poll(uint32_t now);
  static bool authorized(const Access&);

 private:
  using Profile = StoredProfile;
  using Profiles = std::array<Profile, ProfileLimit>;
  Profiles active_{}, staged_{};
  size_t active_count_ = 0, staged_count_ = 0;
  uint64_t revision_ = 1, next_transaction_ = 1, transaction_ = 0, session_ = 0;
  uint32_t next_profile_ = 1, edit_ = 0, opened_ = 0, checking_profile_ = 0;
  uint32_t check_attempt_ = 0;
  uint32_t active_selected_ = 0;
  Phase phase_ = Phase::Idle;
  Error check_error_ = Error::None;
  Error gate(const Access&, uint64_t transaction, uint32_t now);
  Error ticketGate(const Access&, const Ticket&, uint32_t now);
  void clearCandidate();
  void changed();
  static void wipe(void*, size_t);
};
bool validSSID(std::string_view);
bool validPassword(std::string_view);
} // namespace pro::wifi_setup
