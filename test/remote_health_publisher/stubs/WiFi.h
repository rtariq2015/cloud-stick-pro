#pragma once
constexpr int WL_CONNECTED=3;
namespace dav_fixture {inline bool connected=true;}
struct FakeWiFi {int status()const{return dav_fixture::connected?WL_CONNECTED:0;}};
inline FakeWiFi WiFi;
