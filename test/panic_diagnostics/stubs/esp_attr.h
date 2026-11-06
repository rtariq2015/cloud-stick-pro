#pragma once
#define IRAM_ATTR __attribute__((section(".iram1")))
#define RTC_NOINIT_ATTR __attribute__((section(".rtc_noinit")))
