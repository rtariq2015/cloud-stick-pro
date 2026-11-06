// Standalone packet-boundary regression tests; no Wi-Fi, sockets or hardware.
// g++ -std=c++17 -Iinclude -fsanitize=address,undefined -g -O1 test/guest_dns.test.cpp -o /tmp/guest-dns-test
#define GUEST_WIFI_NATIVE_TEST
#include "../src/guest_wifi.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
unsigned checks = 0;
void check(bool condition) {
  ++checks;
  if (!condition) { std::fprintf(stderr, "guest DNS check %u failed\n", checks); std::abort(); }
}
std::vector<uint8_t> query() {
  return {0x12,0x34,0x01,0x00,0x00,0x01,0,0,0,0,0,0,
          3,'w','w','w',7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1};
}
}

int main() {
  auto good = query();
  size_t size = 0;
  check(dnsQuery(good.data(), good.size(), size) && size == good.size()-12);
  for (size_t length = 0; length < good.size(); ++length) {
    check(!dnsQuery(good.data(), length, size));
  }
  auto response = good;
  response[2] |= 0x80;
  check(dnsReply(response.data(), response.size(), 0x1234, good.data()+12, good.size()-12));
  check(!dnsReply(response.data(), response.size(), 0xabcd, good.data()+12, good.size()-12));
  check(!dnsReply(good.data(), good.size(), 0x1234, good.data()+12, good.size()-12));
  response[13] = 'X';
  check(!dnsReply(response.data(), response.size(), 0x1234, good.data()+12, good.size()-12));
  check(!dnsReply(good.data(), 1, 0x1234, good.data()+12, good.size()-12));
  auto invalid = good;
  invalid[4] = 1; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[5] = 2; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[2] |= 0x80; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[2] |= 0x08; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[3] = 1; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[7] = 1; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[9] = 1; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[12] = 64; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[12] = 0xc0; invalid[13] = 12;
  check(!dnsQuery(invalid.data(), invalid.size(), size)); // compression loop
  invalid = good; invalid.back() = 3; check(!dnsQuery(invalid.data(), invalid.size(), size));
  invalid = good; invalid[invalid.size()-3] = 0; check(!dnsQuery(invalid.data(), invalid.size(), size));
  auto edns = good;
  edns[11] = 1;
  edns.insert(edns.end(), {0,0,41,4,208,0,0,0,0,0,0});
  check(dnsQuery(edns.data(), edns.size(), size) && size == good.size()-12);
  auto maximum = std::vector<uint8_t>(good.begin(), good.begin()+12);
  for (unsigned label : {63,63,63,61}) {
    maximum.push_back(label);
    maximum.insert(maximum.end(), label, 'a');
  }
  maximum.insert(maximum.end(), {0,0,28,0,1});
  check(dnsQuery(maximum.data(), maximum.size(), size) && size == QuestionLimit);
  auto longName = maximum;
  longName[204] = 62;
  longName.insert(longName.end()-5, 'a');
  check(!dnsQuery(longName.data(), longName.size(), size));
  auto oversized = good;
  oversized.resize(PacketLimit+1);
  check(!dnsQuery(oversized.data(), oversized.size(), size));
  oversized.resize(PacketLimit);
  check(dnsQuery(oversized.data(), oversized.size(), size));

  check(validMask(0xffffff00U)); check(validMask(0xffff0000U)); check(validMask(0xffffffffU));
  check(!validMask(0)); check(!validMask(0xff00ff00U)); check(!validMask(0x00ffffffU));
  check(overlap(0xc0a80501U,0xfffffe00U,0xc0a80401U,0xffffff00U)); // /23 reaches AP
  check(overlap(0xc0a80001U,0xffff0000U,0xc0a80401U,0xffffff00U));
  check(overlap(0xc0a80410U,0xffffffffU,0xc0a80401U,0xffffff00U));
  check(!overlap(0xc0a80501U,0xffffff00U,0xc0a80401U,0xffffff00U));
  check(!overlap(0x0a000001U,0xff000000U,0xc0a80401U,0xffffff00U));
  check(pro::chooseSetupAddress(0xc0a80420U,0xffffff00U,0xc0a80401U)==0xac1ffe01U);
  check(pro::chooseSetupAddress(0xc0a80120U,0xffff0000U,0xc0a80101U)==0xac1ffe01U);
  check(pro::chooseSetupAddress(0x0a010203U,0xff000000U,0x0a000001U)==0xc0a80401U);
  check(pro::chooseSetupAddress(0xac100003U,0xfff00000U,0xac100001U)==0xc0a80401U);
  check(pro::chooseSetupAddress(0xc0a80420U,0xffff0000U,0xac1ffe10U)==0x0afdfe01U);
  check(pro::chooseSetupAddress(0xc0a80420U,0,0)==0);
  check(pro::chooseSetupAddress(0xc0a80420U,0xff00ff00U,0)==0);
  check(pro::chooseSetupAddress(0xc0a80420U,0x80000000U,0x0afdfe10U)==0);
  // Every prefix length and each private range: chosen subnet must avoid the
  // entire upstream range and its DNS, not merely differ in the third octet.
  for(uint32_t ip:{0x0a010203U,0xac100003U,0xc0a80420U})for(unsigned bits=1;bits<=32;++bits){
    uint32_t mask=0xffffffffU<<(32-bits),dns=ip;
    uint32_t selected=pro::chooseSetupAddress(ip,mask,dns);
    check(selected!=0);
    check(!overlap(ip,mask,selected,pro::SetupMask));
    check((dns&pro::SetupMask)!=(selected&pro::SetupMask));
  }
  check(!elapsed(2999,0,DnsTimeoutMs)); check(elapsed(3000,0,DnsTimeoutMs));
  check(!elapsed(0x00000100U,0xffffff00U,513));
  check(elapsed(0x00000100U,0xffffff00U,512));
  check(ConnectTimeoutMs == 20000 && PendingLimit == 4);
  uint8_t encoded[2]; write16(encoded,0xbeef); check(read16(encoded)==0xbeef);

  // Deterministic hostile datagrams exercise all parser cutoffs under ASan/UBSan.
  std::array<uint8_t,PacketLimit+1> fuzz{};
  uint32_t rng = 0x8f3d721b;
  for (unsigned n=0;n<100000;++n) {
    for (auto& byte : fuzz) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; byte=uint8_t(rng); }
    size_t length = rng % fuzz.size();
    dnsQuery(fuzz.data(),length,size);
    dnsReply(fuzz.data(),length,0x1234,good.data()+12,good.size()-12);
  }
  std::printf("guest DNS: %u checks and 100000 hostile packets passed\n",checks);
}
