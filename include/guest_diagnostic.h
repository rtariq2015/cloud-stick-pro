#pragma once
#include "ble_feature.h"
#if CSP_ENABLE_GUEST_DIAGNOSTIC
namespace pro::guest_diag {
bool start(bool admitted);
void cancel();
void poll();
bool busy();
const char* screen();
void report();
}
#endif
