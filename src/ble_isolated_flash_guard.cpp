#include "ble_feature.h"
#if CSP_BLE_ISOLATED_TEST
#include "ble_isolated_flash_guard.h"
#include "ble_alloc_trace.h"
#include <esp_flash.h>
#include <atomic>
#include <type_traits>

namespace { std::atomic<uint32_t> rawFlashDenials{0}; }

// Complement the partition guard with the pinned public raw mutation entry
// points. These wrappers ignore all buffers/addresses and never call __real.
// They cannot establish that arbitrary ROM/chip-driver internal calls are absent;
// retain the independent link/call audit and pre/post private-state hash gate.
extern "C" esp_err_t __wrap_esp_flash_write(esp_flash_t*,const void*,uint32_t,uint32_t) {
  CSP_TRACE_DENIAL(4);
  rawFlashDenials.fetch_add(1); return ESP_ERR_NOT_SUPPORTED;
}
extern "C" esp_err_t __wrap_esp_flash_write_encrypted(esp_flash_t*,uint32_t,const void*,uint32_t) {
  CSP_TRACE_DENIAL(5);
  rawFlashDenials.fetch_add(1); return ESP_ERR_NOT_SUPPORTED;
}
extern "C" esp_err_t __wrap_esp_flash_erase_region(esp_flash_t*,uint32_t,uint32_t) {
  CSP_TRACE_DENIAL(6);
  rawFlashDenials.fetch_add(1); return ESP_ERR_NOT_SUPPORTED;
}
extern "C" esp_err_t __wrap_esp_flash_erase_chip(esp_flash_t*) {
  CSP_TRACE_DENIAL(7);
  rawFlashDenials.fetch_add(1); return ESP_ERR_NOT_SUPPORTED;
}
static_assert(std::is_same<decltype(&__wrap_esp_flash_write),decltype(&esp_flash_write)>::value,"Pinned raw write ABI changed");
static_assert(std::is_same<decltype(&__wrap_esp_flash_write_encrypted),decltype(&esp_flash_write_encrypted)>::value,"Pinned encrypted write ABI changed");
static_assert(std::is_same<decltype(&__wrap_esp_flash_erase_region),decltype(&esp_flash_erase_region)>::value,"Pinned erase region ABI changed");
static_assert(std::is_same<decltype(&__wrap_esp_flash_erase_chip),decltype(&esp_flash_erase_chip)>::value,"Pinned erase chip ABI changed");
extern "C" uint32_t ble_isolated_raw_flash_denials(void) { return rawFlashDenials.load(); }
#endif
