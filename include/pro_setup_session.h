#pragma once
#include <cstdint>

namespace pro::setup_session {
// Only the distinct reviewed development profile opts in. Historical RC5 and
// the normal BLE-disabled release retain their existing compiled behavior.
#if defined(CSP_PRODUCT_SETUP_DEV_PROFILE) && CSP_PRODUCT_SETUP_DEV_PROFILE
inline constexpr bool Guided=true;
#else
inline constexpr bool Guided=false;
#endif
inline constexpr uint32_t AdvertisingMs=120000,HandshakeMs=30000;
inline constexpr uint32_t AuthenticatedIdleMs=Guided?600000:60000;
}
