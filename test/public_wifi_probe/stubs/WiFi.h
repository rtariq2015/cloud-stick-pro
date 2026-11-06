#pragma once
#include "probe_state.h"
constexpr int WL_CONNECTED=3;
struct FakeWiFi{int status()const{return probe_test::state.wifi;}};
inline FakeWiFi WiFi;
