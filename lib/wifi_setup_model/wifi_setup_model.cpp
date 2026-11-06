#include "wifi_setup_model.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace pro::wifi_setup {
namespace {
bool printable(std::string_view text) {
  for (unsigned char c : text) if (c < 32 || c == 127) return false;
  return true;
}
bool utf8(std::string_view text) {
  for (size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<uint8_t>(text[i]);
    if (c < 128) continue;
    size_t more;
    uint32_t code;
    if (c >= 0xc2 && c <= 0xdf) { more = 1; code = c & 31; }
    else if (c >= 0xe0 && c <= 0xef) { more = 2; code = c & 15; }
    else if (c >= 0xf0 && c <= 0xf4) { more = 3; code = c & 7; }
    else return false;
    if (i + more >= text.size()) return false;
    for (size_t j = 1; j <= more; ++j) {
      const auto next = static_cast<uint8_t>(text[i + j]);
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 63);
    }
    if ((more == 2 && code < 0x800) || (more == 3 && code < 0x10000) ||
        code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    i += more;
  }
  return true;
}
template<size_t N> void put(std::array<char, N>& out, std::string_view in) {
  out.fill(0);
  if (!in.empty()) std::memcpy(out.data(), in.data(), in.size());
}
bool isCheckResult(Error e) {
  return e == Error::None || e == Error::WrongPassword || e == Error::Unavailable ||
         e == Error::Dhcp || e == Error::Dns || e == Error::Captive ||
         e == Error::EndpointUnreachable || e == Error::LocalOnly;
}
}
bool validSSID(std::string_view s) {
  return !s.empty() && s.size() <= 32 && printable(s) && utf8(s);
}
bool validPassword(std::string_view s) {
  // Preserve existing 8..63-byte personal passphrase bounds. No 64-byte raw PSK.
  return s.size() >= 8 && s.size() <= 63 && printable(s) && utf8(s);
}
void Candidate::wipe(void* memory, size_t size) {
  eraseSecretMemory(memory, size);
}
void eraseSecretMemory(void* memory, size_t size) {
  auto* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}
Candidate::~Candidate() {
  wipe(active_.data(), sizeof active_);
  wipe(staged_.data(), sizeof staged_);
}
bool Candidate::authorized(const Access& a) {
  return a.session && a.proof == Proof::DeviceOwner && a.physical_window &&
         !a.terminal_rejection;
}
void Candidate::clearCandidate() {
  wipe(staged_.data(), sizeof staged_);
  staged_count_ = 0;
  transaction_ = session_ = 0;
  edit_ = opened_ = checking_profile_ = check_attempt_ = 0;
  phase_ = Phase::Idle;
  check_error_ = Error::None;
}
void Candidate::poll(uint32_t now) {
  // A future sample caused by callback ordering isn't an elapsed interval.
  const uint32_t elapsed = now - opened_;
  if (phase_ != Phase::Idle && elapsed < 0x80000000U &&
      elapsed >= CandidateLifetimeMs) clearCandidate();
}
void Candidate::closeSession(uint64_t session) {
  if (session && session == session_) clearCandidate();
}
Error Candidate::gate(const Access& a, uint64_t transaction, uint32_t now) {
  if (!authorized(a)) {
    if (a.session && a.session == session_) clearCandidate();
    return Error::Denied;
  }
  const bool expired = phase_ != Phase::Idle &&
      uint32_t(now - opened_) < 0x80000000U &&
      uint32_t(now - opened_) >= CandidateLifetimeMs;
  poll(now);
  if (expired) return Error::Expired;
  if (!transaction || transaction != transaction_ || a.session != session_)
    return Error::StaleTransaction;
  return Error::None;
}
Error Candidate::begin(const Access& a, uint64_t expected, uint32_t now, uint64_t& out) {
  out = 0;
  if (!authorized(a)) return Error::Denied;
  poll(now);
  if (phase_ != Phase::Idle) return Error::Busy;
  if (expected != revision_) return Error::StaleRevision;
  if (next_transaction_ == std::numeric_limits<uint64_t>::max()) return Error::Exhausted;
  staged_ = active_;
  staged_count_ = active_count_;
  transaction_ = next_transaction_++;
  session_ = a.session;
  opened_ = now;
  phase_ = Phase::Editing;
  out = transaction_;
  return Error::None;
}
void Candidate::changed() {
  ++edit_;
  phase_ = Phase::Editing;
  checking_profile_ = 0;
  check_error_ = Error::None;
}
Error Candidate::stage(const Access& a, uint64_t transaction, uint32_t now,
                       const Edit& in, uint32_t& assigned) {
  assigned = 0;
  if (auto e = gate(a, transaction, now); e != Error::None) return e;
  if (!validSSID(in.ssid) || in.priority > 99 ||
      (in.security != Security::Open && in.security != Security::Wpa2Personal) ||
      (in.secret_action != SecretAction::Keep && in.secret_action != SecretAction::Replace &&
       in.secret_action != SecretAction::Forget)) return Error::Invalid;
  if (edit_ == std::numeric_limits<uint32_t>::max()) return Error::Exhausted;
  size_t at = staged_count_;
  for (size_t i = 0; i < staged_count_; ++i) if (staged_[i].id == in.id) at = i;
  if (in.id && at == staged_count_) return Error::Missing;
  if (!in.id && at == ProfileLimit) return Error::Full;
  for (size_t i = 0; i < staged_count_; ++i)
    if (i != at && in.ssid == staged_[i].ssid.data() && in.security == staged_[i].security)
      return Error::Invalid;
  const bool exists = at < staged_count_;
  if (!exists && next_profile_ == std::numeric_limits<uint32_t>::max()) return Error::Exhausted;
  const bool same = exists && in.ssid == staged_[at].ssid.data() &&
                    in.security == staged_[at].security;
  // Explicit Keep never sends a saved password to a different network/security.
  if (in.secret_action == SecretAction::Keep && (!same || !in.secret.empty()))
    return Error::ReplaceSecret;
  if (in.security == Security::Open) {
    if (!in.secret.empty() || in.secret_action == SecretAction::Replace) return Error::Invalid;
  } else if (in.secret_action == SecretAction::Forget ||
             (in.secret_action == SecretAction::Replace && !validPassword(in.secret))) {
    return Error::Invalid;
  }
  if (in.secret_action != SecretAction::Replace && !in.secret.empty()) return Error::Invalid;
  // Build only after validation. Existing candidate survives every failed edit.
  Profile& p = staged_[at];
  p.id = exists ? p.id : next_profile_++;
  put(p.ssid, in.ssid);
  if (in.secret_action != SecretAction::Keep) {
    wipe(p.secret.data(), p.secret.size());
    put(p.secret, in.secret);
  }
  p.security = in.security;
  p.priority = in.priority;
  p.enabled = in.enabled;
  if (!exists) ++staged_count_;
  changed();
  assigned = p.id;
  return Error::None;
}
Error Candidate::forget(const Access& a, uint64_t transaction, uint32_t now,
                        uint32_t id, bool confirm) {
  if (auto e = gate(a, transaction, now); e != Error::None) return e;
  size_t at = staged_count_, enabled = 0;
  for (size_t i = 0; i < staged_count_; ++i) {
    if (staged_[i].id == id) at = i;
    if (staged_[i].enabled) ++enabled;
  }
  if (at == staged_count_) return Error::Missing;
  if (staged_[at].enabled && enabled == 1 && !confirm) return Error::ConfirmationRequired;
  if (edit_ == std::numeric_limits<uint32_t>::max()) return Error::Exhausted;
  wipe(staged_[at].secret.data(), staged_[at].secret.size());
  for (size_t i = at; i + 1 < staged_count_; ++i) staged_[i] = staged_[i + 1];
  --staged_count_;
  wipe(&staged_[staged_count_], sizeof(Profile));
  changed();
  return Error::None;
}
Error Candidate::startCheck(const Access& a, uint64_t transaction, uint32_t now,
                            uint32_t id, Ticket& out) {
  out = {};
  if (auto e = gate(a, transaction, now); e != Error::None) return e;
  if (!a.workers_quiescent) return Error::WorkersBusy;
  if (phase_ == Phase::Checking) return Error::Busy;
  if (check_attempt_ == std::numeric_limits<uint32_t>::max()) return Error::Exhausted;
  auto p = std::find_if(staged_.begin(), staged_.begin() + staged_count_,
                        [id](const Profile& row) { return row.id == id && row.enabled; });
  if (p == staged_.begin() + staged_count_) return Error::Missing;
  phase_ = Phase::Checking;
  check_error_ = Error::None;
  checking_profile_ = id;
  out = {session_, transaction_, edit_, id, ++check_attempt_};
  return Error::None;
}
Error Candidate::ticketGate(const Access& a, const Ticket& t, uint32_t now) {
  if (auto e = gate(a, t.transaction, now); e != Error::None) return e;
  if (phase_ != Phase::Checking || !t.profile || t.profile != checking_profile_ ||
      t.session != session_ || t.edit != edit_ || t.attempt != check_attempt_)
    return Error::StaleTransaction;
  return Error::None;
}
Error Candidate::borrowCheckProfile(const Access& a, const Ticket& t, uint32_t now,
                                    CheckVisitor visitor, void* context) {
  if (auto e = ticketGate(a, t, now); e != Error::None) return e;
  if (!a.workers_quiescent) return Error::WorkersBusy;
  if (!visitor) return Error::Invalid;
  for (size_t i = 0; i < staged_count_; ++i) if (staged_[i].id == t.profile) {
    const Profile& p = staged_[i];
    visitor(context, {p.id, p.ssid.data(), p.secret.data(), p.security});
    return Error::None;
  }
  return Error::Missing;
}
Error Candidate::finishCheck(const Access& a, const Ticket& t, uint32_t now, Error result) {
  if (auto e = ticketGate(a, t, now); e != Error::None) return e;
  if (!a.workers_quiescent) return Error::WorkersBusy;
  if (!isCheckResult(result)) return Error::Invalid;
  check_error_ = result;
  phase_ = result == Error::None || result == Error::LocalOnly ? Phase::Ready : Phase::Failed;
  return Error::None;
}
Error Candidate::commitInMemory(const Access& a, uint64_t transaction, uint32_t edit,
                                uint64_t expected, uint32_t now) {
  if (auto e = gate(a, transaction, now); e != Error::None) return e;
  if (expected != revision_) return Error::StaleRevision;
  if (edit != edit_) return Error::StaleTransaction;
  if (!a.workers_quiescent) return Error::WorkersBusy;
  if (phase_ != Phase::Ready) return Error::CheckRequired;
  if (check_error_ == Error::LocalOnly) return Error::CheckRequired;
  if (revision_ == std::numeric_limits<uint64_t>::max()) return Error::Exhausted;
  wipe(active_.data(), sizeof active_);
  active_ = staged_;
  active_count_ = staged_count_;
  active_selected_ = checking_profile_;
  ++revision_;
  clearCandidate();
  return Error::None;
}
bool validStored(const StoredNetworks& s) {
  if (!s.revision || !s.next_id || s.count > ProfileLimit) return false;
  bool selected = s.selected == 0 && s.count == 0;
  for (size_t i = 0; i < s.count; ++i) {
    const auto& p = s.profiles[i];
    const size_t name = strnlen(p.ssid.data(), p.ssid.size());
    const size_t secret = strnlen(p.secret.data(), p.secret.size());
    if (!p.id || p.id >= s.next_id || p.priority > 99 ||
        !validSSID({p.ssid.data(), name}) || secret >= p.secret.size()) return false;
    for (size_t j=name+1;j<p.ssid.size();++j) if(p.ssid[j]) return false;
    for (size_t j=secret+1;j<p.secret.size();++j) if(p.secret[j]) return false;
    if (p.security == Security::Open) { if (secret) return false; }
    else if (p.security != Security::Wpa2Personal || !validPassword({p.secret.data(), secret})) return false;
    for (size_t j = 0; j < i; ++j)
      if (s.profiles[j].id == p.id ||
          (s.profiles[j].security == p.security && s.profiles[j].ssid == p.ssid)) return false;
    if (p.id == s.selected && p.enabled) selected = true;
  }
  return selected;
}
Error Candidate::preparePersistent(const Access& a, uint64_t tx, uint32_t edit,
                                   uint64_t revision, uint32_t now,
                                   bool accept_local, StoredNetworks& out) {
  eraseSecretMemory(&out, sizeof out);
  if (auto e = gate(a, tx, now); e != Error::None) return e;
  if (revision != revision_) return Error::StaleRevision;
  if (edit != edit_) return Error::StaleTransaction;
  if (!a.workers_quiescent) return Error::WorkersBusy;
  if (phase_ != Phase::Ready || (check_error_ == Error::LocalOnly && !accept_local))
    return Error::CheckRequired;
  if (revision_ == UINT64_MAX) return Error::Exhausted;
  out.revision = revision_ + 1; out.next_id = next_profile_;
  out.selected = checking_profile_; out.count = staged_count_; out.profiles = staged_;
  return validStored(out) ? Error::None : Error::Invalid;
}
bool Candidate::loadTrusted(const StoredNetworks& saved) {
  if (!validStored(saved)) return false;
  clearCandidate();
  wipe(active_.data(), sizeof active_);
  active_ = saved.profiles; active_count_ = saved.count;
  revision_ = saved.revision; next_profile_ = saved.next_id;
  active_selected_ = saved.selected;
  return true;
}
bool Candidate::exportActive(StoredNetworks& out) const {
  out = {};out.revision = revision_;out.next_id = next_profile_;
  out.selected = active_selected_;out.count = active_count_;out.profiles = active_;
  return validStored(out);
}
Error Candidate::abort(const Access& a, uint64_t transaction, uint32_t now) {
  if (auto e = gate(a, transaction, now); e != Error::None) return e;
  clearCandidate();
  return Error::None;
}
Error Candidate::snapshot(const Access& a, uint32_t now, bool candidate, Snapshot& out) {
  out = {};
  if (!authorized(a)) {
    if (a.session && a.session == session_) clearCandidate();
    return Error::Denied;
  }
  poll(now);
  if (candidate && (phase_ == Phase::Idle || a.session != session_)) return Error::StaleTransaction;
  out.revision = revision_;
  const auto& profiles = candidate ? staged_ : active_;
  out.count = candidate ? staged_count_ : active_count_;
  if (candidate) {
    out.transaction = transaction_;
    out.edit = edit_;
    out.phase = phase_;
    out.check_error = check_error_;
  }
  for (size_t i = 0; i < out.count; ++i) {
    const Profile& p = profiles[i];
    out.profiles[i] = {p.id, p.ssid, p.security, p.priority, p.enabled, p.secret[0] != 0};
  }
  return Error::None;
}
} // namespace pro::wifi_setup
