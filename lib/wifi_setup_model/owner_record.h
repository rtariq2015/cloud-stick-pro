#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace pro::wifi_setup {
constexpr size_t OwnerRecordSize=432;
constexpr const char* OwnerUsername="csp-owner";
struct OwnerRecord {
  std::array<uint8_t,6> mac{};
  std::array<uint8_t,16> salt{};
  std::array<uint8_t,384> verifier{};
};
// CRC is a corruption check, not encryption or authentication. Only physical
// first enrollment may select this record; Security 2 subsequently checks proof.
bool decodeOwner(const uint8_t*,size_t,const uint8_t expected_mac[6],OwnerRecord&);
} // namespace pro::wifi_setup
