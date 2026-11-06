#pragma once
constexpr int WL_CONNECTED=3;
struct FakeWiFi {int status()const{return WL_CONNECTED;}};
inline FakeWiFi WiFi;
