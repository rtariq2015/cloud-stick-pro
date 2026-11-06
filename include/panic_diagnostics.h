#pragma once
#ifndef CSP_PANIC_DIAGNOSTICS
#define CSP_PANIC_DIAGNOSTICS 0
#endif
#if CSP_PANIC_DIAGNOSTICS != 0 && CSP_PANIC_DIAGNOSTICS != 1
#error CSP_PANIC_DIAGNOSTICS must be zero or one
#endif
#if CSP_PANIC_DIAGNOSTICS
#if !CSP_PRODUCT_LAB4_PROFILE
#error Panic diagnostics are restricted to the explicit lab4 profile
#endif
#include <cstddef>
// Lab-only numeric evidence, not a panic recovery or reset-policy replacement.
// Call once, before radio initialization; the SDK still owns panic processing.
constexpr size_t PanicDiagnosticPcLimit=16,PanicDiagnosticJsonLimit=384;
void panic_diagnostics_begin();
// Reads the boot-time RAM copy only. No UART, filesystem, NVS or radio access.
// On failure the destination, if nonempty, is an empty C string.
bool panic_diagnostics_json(char* destination,size_t capacity);
#endif
