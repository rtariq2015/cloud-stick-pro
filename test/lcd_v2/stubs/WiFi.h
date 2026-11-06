#pragma once
#include "IPAddress.h"
struct FakeWiFi {IPAddress localIP()const{return {};}};
inline FakeWiFi WiFi;
