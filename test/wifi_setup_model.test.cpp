#include "wifi_setup_model.h"
#include "pro_core.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <new>
#include <random>
#include <string>

using namespace pro::wifi_setup;
namespace {
size_t assertions = 0;
#define CHECK(x) do { ++assertions; if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (0)
const Access owner{51, Proof::DeviceOwner, true, false, true};
Edit network(std::string_view name = "CSP Test Private", std::string_view secret = "testpass123") {
  return {0, name, Security::Wpa2Personal, SecretAction::Replace, secret, 2, true};
}
Snapshot view(Candidate& c, bool staged, uint32_t now = 0) {
  Snapshot s;
  CHECK(c.snapshot(owner, now, staged, s) == Error::None);
  CHECK(!s.durable);
  return s;
}
uint64_t begin(Candidate& c, uint32_t now = 0) {
  uint64_t tx = 0;
  CHECK(c.begin(owner, view(c, false, now).revision, now, tx) == Error::None);
  CHECK(tx != 0);
  return tx;
}
void checked(Candidate& c, uint64_t tx, uint32_t id, uint32_t now = 0) {
  Ticket t;
  CHECK(c.startCheck(owner, tx, now, id, t) == Error::None);
  CHECK(c.finishCheck(owner, t, now, Error::None) == Error::None);
}
void commit(Candidate& c, uint64_t tx, uint32_t now = 0) {
  const auto s = view(c, true, now);
  CHECK(c.commitInMemory(owner, tx, s.edit, s.revision, now) == Error::None);
}
void authAndBounds() {
  Candidate c;
  uint64_t tx = 99;
  for (Proof p : {Proof::None, Proof::LegacyShared, Proof::SyntheticM1}) {
    Access bad = owner; bad.proof = p;
    CHECK(c.begin(bad, 1, 0, tx) == Error::Denied && tx == 0);
    Snapshot s; s.count = 99;
    CHECK(c.snapshot(bad, 0, false, s) == Error::Denied && s.count == 0 && s.revision == 0);
  }
  Access bad = owner; bad.session = 0;
  CHECK(c.begin(bad, 1, 0, tx) == Error::Denied);
  bad = owner; bad.physical_window = false;
  CHECK(c.begin(bad, 1, 0, tx) == Error::Denied);
  CHECK(c.begin(owner, 2, 0, tx) == Error::StaleRevision);
  tx = begin(c);
  uint64_t another = 99;
  CHECK(c.begin(owner, 1, 0, another) == Error::Busy && another == 0);
  uint32_t id;
  CHECK(c.stage(owner, tx, 0, network("CSP Test Private", "short"), id) == Error::Invalid && id == 0);
  CHECK(c.stage(owner, tx, 0, network(std::string(33, 'x')), id) == Error::Invalid);
  CHECK(c.stage(owner, tx, 0, network(""), id) == Error::Invalid);
  CHECK(c.stage(owner, tx, 0, network("\xC0\x80"), id) == Error::Invalid);
  CHECK(c.stage(owner, tx, 0, network("\xED\xA0\x80"), id) == Error::Invalid);
  CHECK(c.stage(owner, tx, 0, network(std::string("bad\0name", 8)), id) == Error::Invalid);
  CHECK(c.stage(owner, tx, 0, network("CSP Test Private", std::string(64, 'x')), id) == Error::Invalid);
  CHECK(c.stage(owner, tx, 0, network("\xE2\x98\x83"), id) == Error::None);
  const uint32_t first = id;
  Edit e = network("CSP Test Open", ""); e.security = Security::Open;
  CHECK(c.stage(owner, tx, 0, e, id) == Error::Invalid); // requires explicit Forget
  e.secret_action = SecretAction::Forget;
  CHECK(c.stage(owner, tx, 0, e, id) == Error::None);
  auto s = view(c, true);
  CHECK(s.count == 2 && !s.profiles[1].has_secret && s.profiles[0].has_secret);
  CHECK(c.stage(owner, tx, 0, e, id) == Error::Invalid); // duplicate identity
  for (int i = 2; i < 8; ++i) {
    const std::string name = "CSP Test " + std::to_string(i);
    CHECK(c.stage(owner, tx, 0, network(name), id) == Error::None);
  }
  CHECK(view(c, true).count == 8);
  CHECK(c.stage(owner, tx, 0, network("CSP Test ninth"), id) == Error::Full);
  CHECK(c.forget(owner, tx, 0, first, false) == Error::None);
  CHECK(c.stage(owner, tx, 0, network("CSP Test next"), id) == Error::None);
  CHECK(id > 8 && id != first);
  CHECK(view(c, false).count == 0); // staging never changes active config
  CHECK(c.abort(owner, tx, 0) == Error::None);
  CHECK(view(c, false).count == 0);
}
void transactionAndCheck() {
  Candidate c;
  auto tx = begin(c);
  uint32_t id = 0;
  CHECK(c.stage(owner, tx, 0, network(), id) == Error::None);
  auto s = view(c, true);
  CHECK(c.commitInMemory(owner, tx, s.edit, s.revision, 0) == Error::CheckRequired);
  Access busy = owner; busy.workers_quiescent = false;
  Ticket t;
  CHECK(c.startCheck(busy, tx, 0, id, t) == Error::WorkersBusy && t.transaction == 0);
  CHECK(c.startCheck(owner, tx, 0, id, t) == Error::None);
  Ticket other;
  CHECK(c.startCheck(owner, tx, 0, id, other) == Error::Busy && other.transaction == 0);
  CHECK(c.finishCheck(owner, t, 0, Error::Denied) == Error::Invalid);
  bool visited = false;
  CHECK(c.borrowCheckProfile(owner, t, 0, [](void* p, const CheckProfile& row) {
    *static_cast<bool*>(p) = row.secret == "testpass123" && row.ssid == "CSP Test Private";
  }, &visited) == Error::None && visited);
  CHECK(c.finishCheck(owner, t, 0, Error::WrongPassword) == Error::None);
  s = view(c, true);
  CHECK(s.phase == Phase::Failed && s.check_error == Error::WrongPassword);
  CHECK(c.commitInMemory(owner, tx, s.edit, 1, 0) == Error::CheckRequired);
  CHECK(view(c, false).revision == 1 && view(c, false).count == 0);
  Ticket retry;
  CHECK(c.startCheck(owner, tx, 0, id, retry) == Error::None);
  CHECK(retry.attempt != t.attempt);
  CHECK(c.finishCheck(owner, t, 0, Error::None) == Error::StaleTransaction);
  CHECK(c.finishCheck(busy, retry, 0, Error::None) == Error::WorkersBusy);
  CHECK(c.finishCheck(owner, retry, 0, Error::Captive) == Error::None);
  CHECK(view(c, true).phase == Phase::Failed); // portal is not usable
  checked(c, tx, id);
  s = view(c, true);
  CHECK(c.commitInMemory(owner, tx, s.edit + 1, 1, 0) == Error::StaleTransaction);
  CHECK(c.commitInMemory(owner, tx, s.edit, 2, 0) == Error::StaleRevision);
  CHECK(c.commitInMemory(busy, tx, s.edit, 1, 0) == Error::WorkersBusy);
  commit(c, tx);
  s = view(c, false);
  CHECK(s.revision == 2 && s.count == 1 && s.profiles[0].id == id);
  CHECK(c.commitInMemory(owner, tx, 1, 1, 0) == Error::StaleTransaction);

  tx = begin(c);
  Edit e = network(); e.id = id; e.secret_action = SecretAction::Keep; e.secret = "";
  e.ssid = "CSP Test Changed";
  uint32_t assigned;
  CHECK(c.stage(owner, tx, 0, e, assigned) == Error::ReplaceSecret);
  e.ssid = "CSP Test Private"; e.priority = 1;
  CHECK(c.stage(owner, tx, 0, e, assigned) == Error::None);
  CHECK(c.startCheck(owner, tx, 0, id, t) == Error::None);
  e.priority = 3;
  CHECK(c.stage(owner, tx, 0, e, assigned) == Error::None); // cancels old job result
  CHECK(c.finishCheck(owner, t, 0, Error::None) == Error::StaleTransaction);
  CHECK(c.forget(owner, tx, 0, id, false) == Error::ConfirmationRequired);
  CHECK(c.abort(owner, tx, 0) == Error::None);
  CHECK(view(c, false).profiles[0].priority == 2); // original untouched
}
void lifetimeAndRevocation() {
  Candidate c;
  uint32_t id;
  auto tx = begin(c, UINT32_MAX - 500);
  CHECK(c.stage(owner, tx, UINT32_MAX - 400, network(), id) == Error::None);
  Ticket old;
  CHECK(c.startCheck(owner, tx, 100, id, old) == Error::None); // ordinary clock wrap
  CHECK(c.finishCheck(owner, old, 200, Error::None) == Error::None);
  CHECK(c.commitInMemory(owner, tx, 1, 1, 300) == Error::None);
  tx = begin(c, 10000);
  CHECK(c.stage(owner, tx, 9999, network("CSP Test second"), id) == Error::None); // older sample
  CHECK(c.startCheck(owner, tx, 10001, id, old) == Error::None);
  CHECK(c.finishCheck(owner, old, 130000, Error::None) == Error::Expired);
  CHECK(view(c, false, 130000).revision == 2);
  tx = begin(c, 130001);
  CHECK(c.finishCheck(owner, old, 130002, Error::None) == Error::StaleTransaction);
  Access stranger = owner; ++stranger.session;
  uint32_t assigned;
  CHECK(c.stage(stranger, tx, 130002, network(), assigned) == Error::StaleTransaction);
  c.closeSession(stranger.session);
  CHECK(view(c, true, 130003).transaction == tx);
  Access rejected = owner; rejected.terminal_rejection = true;
  CHECK(c.stage(rejected, tx, 130004, network(), assigned) == Error::Denied);
  CHECK(c.stage(owner, tx, 130005, network(), assigned) == Error::StaleTransaction);
  tx = begin(c, 140000);
  c.closeSession(owner.session);
  CHECK(c.stage(owner, tx, 140001, network(), assigned) == Error::StaleTransaction);
  tx = begin(c, 150000);
  c.poll(270000);
  CHECK(c.stage(owner, tx, 270001, network(), assigned) == Error::StaleTransaction);
  CHECK(view(c, false, 270001).revision == 2);
}
void hostileValidation() {
  std::mt19937 random(0x43535032);
  for (size_t i = 0; i < 100000; ++i) {
    std::string ssid(random() % 40, ' '), password(random() % 70, ' ');
    for (char& c : ssid) c = char(random() & 255);
    for (char& c : password) c = char(random() & 255);
    // New UTF-8 restriction may reject more; never broaden legacy acceptance.
    if (validSSID(ssid) && validPassword(password)) CHECK(pro::validWiFi(ssid, password, false));
    if (validSSID(ssid)) CHECK(pro::validWiFi(ssid, "", true));
    CHECK(!validSSID(ssid) || ssid.size() <= 32);
  }
  CHECK(validSSID(std::string(32, 'x')) && !validSSID(std::string(33, 'x')));
  CHECK(validPassword(std::string(8, 'x')) && validPassword(std::string(63, 'x')));
  CHECK(!validPassword(std::string(7, 'x')) && !validPassword(std::string(64, 'x')));
}
void repeatedTransactions() {
  Candidate c;
  uint64_t last_revision = 1;
  for (uint32_t cycle = 0; cycle < 250; ++cycle) {
    const uint32_t now = cycle * 1000;
    const auto tx = begin(c, now);
    const auto active = view(c, false, now);
    Edit e = network();
    if (active.count) e.id = active.profiles[0].id;
    e.priority = cycle % 100;
    uint32_t id;
    CHECK(c.stage(owner, tx, now, e, id) == Error::None);
    Ticket t;
    CHECK(c.startCheck(owner, tx, now, id, t) == Error::None);
    for (auto error : {Error::WrongPassword, Error::Dhcp, Error::Dns,
                       Error::Captive, Error::Unavailable, Error::EndpointUnreachable}) {
      CHECK(c.finishCheck(owner, t, now, error) == Error::None);
      CHECK(view(c, false, now).revision == last_revision);
      const auto s = view(c, true, now);
      CHECK(c.commitInMemory(owner, tx, s.edit, s.revision, now) == Error::CheckRequired);
      Ticket retry;
      CHECK(c.startCheck(owner, tx, now, id, retry) == Error::None);
      CHECK(c.finishCheck(owner, t, now, Error::None) == Error::StaleTransaction);
      t = retry;
    }
    Ticket wrong = t; ++wrong.session;
    CHECK(c.finishCheck(owner, wrong, now, Error::None) == Error::StaleTransaction);
    wrong = t; ++wrong.edit;
    CHECK(c.finishCheck(owner, wrong, now, Error::None) == Error::StaleTransaction);
    CHECK(c.finishCheck(owner, t, now, Error::None) == Error::None);
    commit(c, tx, now);
    ++last_revision;
    CHECK(view(c, false, now).revision == last_revision);
    CHECK(view(c, false, now).count == 1);
    CHECK(c.finishCheck(owner, t, now, Error::None) == Error::StaleTransaction);
  }
}
size_t copies(const void* memory, size_t length, std::string_view secret) {
  const auto* p = static_cast<const unsigned char*>(memory);
  size_t count = 0;
  for (size_t i = 0; i + secret.size() <= length; ++i)
    if (std::memcmp(p + i, secret.data(), secret.size()) == 0) ++count;
  return count;
}
void secretBufferLifetime() {
  const std::string_view secret = "CSP_SYNTHETIC_ONLY_LONG_SECRET_4721";
  for (const auto method : {0, 1, 2, 3}) {
    Candidate c;
    const auto tx = begin(c);
    uint32_t id;
    CHECK(c.stage(owner, tx, 0, network("CSP Test Private", secret), id) == Error::None);
    CHECK(copies(&c, sizeof c, secret) == 1);
    Snapshot s = view(c, true);
    CHECK(copies(&s, sizeof s, secret) == 0);
    if (method == 0) CHECK(c.abort(owner, tx, 0) == Error::None);
    if (method == 1) c.poll(CandidateLifetimeMs);
    if (method == 2) c.closeSession(owner.session);
    if (method == 3) {
      Access bad = owner; bad.terminal_rejection = true;
      CHECK(c.snapshot(bad, 0, true, s) == Error::Denied);
    }
    CHECK(copies(&c, sizeof c, secret) == 0);
  }
  alignas(Candidate) unsigned char memory[sizeof(Candidate)]{};
  auto* c = new(memory) Candidate();
  const auto tx = begin(*c);
  uint32_t id;
  CHECK(c->stage(owner, tx, 0, network("CSP Test Private", secret), id) == Error::None);
  checked(*c, tx, id);
  commit(*c, tx);
  CHECK(copies(memory, sizeof memory, secret) == 1); // active only; staged was cleared
  c->~Candidate();
  CHECK(copies(memory, sizeof memory, secret) == 0);
}
}
int main() {
  authAndBounds(); transactionAndCheck(); lifetimeAndRevocation(); hostileValidation(); repeatedTransactions(); secretBufferLifetime();
  CHECK(sizeof(Candidate) < 2048);
  CHECK(sizeof(Snapshot) < 512);
  std::cout << "PASS " << assertions << " checks; 100000 hostile validation pairs; Candidate="
            << sizeof(Candidate) << " Snapshot=" << sizeof(Snapshot) << " bytes\n";
}
