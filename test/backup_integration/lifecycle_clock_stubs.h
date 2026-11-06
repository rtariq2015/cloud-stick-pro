#pragma once
#define millis fixedFixtureMillis
#include "runtime_stubs.h"
#undef millis
// Unlike the portal clock fixture this must advance during the actual UART
// BENCH_WINDOW handler, which stamps the new window after loop's older sample.
inline uint32_t millis(){return testNow++;}
