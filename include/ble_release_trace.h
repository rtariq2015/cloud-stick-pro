// SPDX-License-Identifier: MIT
#pragma once
#include "ble_feature.h"
#ifndef CSP_BLE_RELEASE_DIAGNOSTICS
#define CSP_BLE_RELEASE_DIAGNOSTICS 0
#endif
#ifndef CSP_PRODUCT_RELEASE_LAB_PROFILE
#define CSP_PRODUCT_RELEASE_LAB_PROFILE 0
#endif
#if CSP_BLE_RELEASE_DIAGNOSTICS != 0 && CSP_BLE_RELEASE_DIAGNOSTICS != 1
#error CSP_BLE_RELEASE_DIAGNOSTICS must be 0 or 1
#endif
#if CSP_BLE_RELEASE_DIAGNOSTICS && (!CSP_PRODUCT_RELEASE_LAB_PROFILE || !CSP_ENABLE_BACKUP_SETUP || !CSP_USB_BENCH_AUTOMATION || CSP_BLE_ALLOC_TRACE)
#error Release observation requires its separate combined diagnostic profile
#endif
#if CSP_PRODUCT_RELEASE_LAB_PROFILE && !CSP_BLE_RELEASE_DIAGNOSTICS
#error Release diagnostic identity requires its observer
#endif
#if CSP_BLE_RELEASE_DIAGNOSTICS
#include "pro_heap_span.h"
#include "pro_alloc_trace.h"
void ble_release_trace_begin();
void ble_release_trace_finish();
pro::trace::HeapSpan ble_release_trace_span();
pro::trace::Summary ble_release_trace_summary();
size_t ble_release_trace_page(size_t,pro::trace::Record*,size_t);
#endif
