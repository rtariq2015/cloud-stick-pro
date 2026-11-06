#pragma once
#ifndef CSP_PRODUCT_LAB6_PROFILE
#define CSP_PRODUCT_LAB6_PROFILE 0
#endif
#if CSP_PRODUCT_LAB6_PROFILE != 0 && CSP_PRODUCT_LAB6_PROFILE != 1
#error CSP_PRODUCT_LAB6_PROFILE must be zero or one
#endif
#if CSP_PRODUCT_LAB6_PROFILE && (!CSP_ENABLE_OWNED_GUEST_ROUTE || !CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE || !CSP_USB_BENCH_AUTOMATION)
#error Lab6 route diagnostics require the explicit public routing USB bench
#endif
