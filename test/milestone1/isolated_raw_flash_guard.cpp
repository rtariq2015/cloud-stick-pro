#include "ble_isolated_flash_guard.h"
#include <esp_flash.h>
#include <cassert>
#include <cstdio>
extern "C" esp_err_t __wrap_esp_flash_write(esp_flash_t*,const void*,uint32_t,uint32_t);
extern "C" esp_err_t __wrap_esp_flash_write_encrypted(esp_flash_t*,uint32_t,const void*,uint32_t);
extern "C" esp_err_t __wrap_esp_flash_erase_region(esp_flash_t*,uint32_t,uint32_t);
extern "C" esp_err_t __wrap_esp_flash_erase_chip(esp_flash_t*);
int main() {
    // Invalid pointers deliberately prove denial does not inspect/mutate data.
    auto chip=reinterpret_cast<esp_flash_t*>(1);
    auto data=reinterpret_cast<void*>(1);
    assert(ble_isolated_raw_flash_denials()==0);
    assert(__wrap_esp_flash_write(chip,data,0xffffffff,0xffffffff)==ESP_ERR_NOT_SUPPORTED);
    assert(__wrap_esp_flash_write_encrypted(chip,0xffffffff,data,0xffffffff)==ESP_ERR_NOT_SUPPORTED);
    assert(__wrap_esp_flash_erase_region(chip,0xffffffff,0xffffffff)==ESP_ERR_NOT_SUPPORTED);
    assert(__wrap_esp_flash_erase_chip(chip)==ESP_ERR_NOT_SUPPORTED);
    assert(ble_isolated_raw_flash_denials()==4);
    puts("isolated raw flash guard: 4 mutation entry points deny without touching input or invoking real APIs");
}
