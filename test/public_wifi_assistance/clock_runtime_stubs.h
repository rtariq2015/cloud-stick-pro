#pragma once
// Advance every actual millis() observation, so owner-loop timestamps can be
// older than an immediately dispatched mailbox command. Shared stubs unchanged.
#define millis fixedFixtureMillis
#include "../backup_integration/runtime_stubs.h"
#undef millis
#define CSP_PUBLIC_CLOCK_TEST 1
inline bool advancePublicClock=false;
inline uint32_t millis(){return advancePublicClock?testNow++:testNow;}
