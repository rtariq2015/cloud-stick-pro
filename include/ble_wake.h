// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Pairing keys authorize opening a finite BLE window, never a management
// session. The existing Security 2 owner login is still mandatory afterward.
#ifndef CSP_ENABLE_BLE_WIFI_WAKE
#define CSP_ENABLE_BLE_WIFI_WAKE 0
#endif
#if CSP_ENABLE_BLE_WIFI_WAKE != 0 && CSP_ENABLE_BLE_WIFI_WAKE != 1
#error CSP_ENABLE_BLE_WIFI_WAKE must be 0 or 1
#endif
#if CSP_ENABLE_BLE_WIFI_WAKE && (!CSP_ENABLE_BACKUP_SETUP || !CSP_ENABLE_OWNER_LOGIN || !CSP_ENABLE_LOCAL_DASHBOARD)
#error Wi-Fi wake requires the authenticated combined dashboard profile
#endif

namespace pro::ble_wake {
constexpr size_t PublicKeyBytes=65,NonceBytes=32,IdBytes=16,RecordBytes=87;
constexpr uint32_t ChallengeMs=15000,RetryMs=2000,WakeCooldownMs=30000;
using PublicKey=std::array<uint8_t,PublicKeyBytes>;
using Nonce=std::array<uint8_t,NonceBytes>;
using Id=std::array<uint8_t,IdBytes>;
using Record=std::array<uint8_t,RecordBytes>;
struct Pairing {
  std::array<uint8_t,6> device{};
  uint64_t owner_revision=0;
  PublicKey key{};
};
// CRC checks torn/corrupt storage; trust comes only from the authenticated
// pairing command, exact board identity and current owner-record revision.
bool encode(const Pairing&,Record&);
bool decode(const Record&,const std::array<uint8_t,6>&,uint64_t,Pairing&);
constexpr size_t MessageBytes=18+IdBytes+NonceBytes;
using Message=std::array<uint8_t,MessageBytes>;
Message message(const Id&,const Nonce&);
using Random=bool (*)(void*,Nonce&);
using Verify=bool (*)(void*,const PublicKey&,const Message&,const uint8_t*,size_t);
enum class Result:uint8_t {Ready,Unavailable,Busy,Expired,Denied};
class Gate {
 public:
  void bind(const Pairing&,const Id&);
  void clear();
  Result challenge(uint32_t,uint64_t,bool,Random,void*,Nonce&);
  Result authorize(uint32_t,uint64_t,bool,const Nonce&,const uint8_t*,size_t,Verify,void*);
  const Id& id()const{return id_;}
  bool paired(uint64_t revision)const{return bound_&&pairing_.owner_revision==revision;}
 private:
  Pairing pairing_{};
  Id id_{};
  Nonce nonce_{};
  uint32_t issued_=0,attempted_=0,woken_=0;
  bool bound_=false,pending_=false,attempted_once_=false,woken_once_=false;
};
}
