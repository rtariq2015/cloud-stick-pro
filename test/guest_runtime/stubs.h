#pragma once
// Preserve existing fixtures, adding only the station address observed by the
// dashboard branch. The actual radio and networking remain simulated.
#define FakeWiFi GuestBaseWiFi
#include "../backup_integration/runtime_stubs.h"
#undef FakeWiFi
struct RuntimeAddress {
  uint32_t value=0x0a010203;
  explicit operator uint32_t()const{return value;}
  std::string toString()const{return "10.1.2.3";}
};
struct FakeWiFi:GuestBaseWiFi {
  RuntimeAddress localIP(){return {};}
  RuntimeAddress subnetMask(){return {0xffffff00};}
};
#define WiFi guestRuntimeWiFi
extern FakeWiFi guestRuntimeWiFi;
inline void configTzTime(const char* tz,const char* a,const char*){configTzTime(tz,a);}
