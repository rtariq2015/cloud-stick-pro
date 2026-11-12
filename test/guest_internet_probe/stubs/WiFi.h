#pragma once
#include <cstdint>
constexpr int WL_CONNECTED=3;
struct FakeWifi {int state=WL_CONNECTED;int status(){return state;}};
inline FakeWifi WiFi;

struct IPAddress {uint32_t value=0;IPAddress()=default;explicit IPAddress(uint32_t v):value(v){};};
