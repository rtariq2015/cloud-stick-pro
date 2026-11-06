#pragma once
// Host has mbedTLS2.28; ESP-IDF5.5 uses the renamed mbedTLS3 entry points.
// These aliases select the equivalent error-returning host APIs, not new crypto.
#include <assert.h>
#include <mbedtls/sha512.h>
#define static_assert _Static_assert
#define mbedtls_sha512_starts mbedtls_sha512_starts_ret
#define mbedtls_sha512_update mbedtls_sha512_update_ret
#define mbedtls_sha512_finish mbedtls_sha512_finish_ret
#define mbedtls_sha512 mbedtls_sha512_ret
