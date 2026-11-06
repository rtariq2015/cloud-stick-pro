#pragma once
#include "ble_feature.h"
#include <stdint.h>
#if CSP_BLE_ALLOC_TRACE
#include "pro_alloc_trace.h"
#include "pro_heap_span.h"
void ble_alloc_trace_begin();
void ble_alloc_trace_phase(pro::trace::Phase);
void ble_alloc_trace_stop(uint32_t now);
void ble_alloc_trace_poll(uint32_t now);
pro::trace::Summary ble_alloc_trace_summary();
size_t ble_alloc_trace_page(size_t offset,pro::trace::Record*,size_t count);
struct BleDeniedCall {uintptr_t caller;uint32_t operation,phase;};
BleDeniedCall ble_alloc_trace_denied_call();
pro::trace::HeapSpan ble_alloc_trace_heap_span();
void ble_alloc_trace_denial(uint32_t,uintptr_t);
#define CSP_TRACE_DENIAL(operation) ble_alloc_trace_denial(operation,reinterpret_cast<uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0))))
#else
#define CSP_TRACE_DENIAL(operation) ((void)0)
#endif
