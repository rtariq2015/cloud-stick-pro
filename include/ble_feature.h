#pragma once

// Separate service image only: bounded synthetic cache exercise, never a
// settings endpoint and never enabled in the released application profile.
#ifndef CSP_NVS_CACHE_LAB
#define CSP_NVS_CACHE_LAB 0
#endif
#if CSP_NVS_CACHE_LAB != 0 && CSP_NVS_CACHE_LAB != 1
#error CSP_NVS_CACHE_LAB must be 0 or 1
#endif
// Rebuild only the default NVS RAM cache after verified radio/worker teardown.
#ifndef CSP_ENABLE_BLE_NVS_CACHE_RELEASE
#define CSP_ENABLE_BLE_NVS_CACHE_RELEASE 0
#endif
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE != 0 && CSP_ENABLE_BLE_NVS_CACHE_RELEASE != 1
#error CSP_ENABLE_BLE_NVS_CACHE_RELEASE must be 0 or 1
#endif
// The BLE settlement candidate drains delayed SDK cleanup within the existing
// closing deadline. Older profiles retain their original two-second limit.
#ifndef CSP_ENABLE_BLE_RELEASE_SETTLEMENT
#define CSP_ENABLE_BLE_RELEASE_SETTLEMENT 0
#endif
#if CSP_ENABLE_BLE_RELEASE_SETTLEMENT != 0 && CSP_ENABLE_BLE_RELEASE_SETTLEMENT != 1
#error CSP_ENABLE_BLE_RELEASE_SETTLEMENT must be 0 or 1
#endif
// Private candidate only: delayed network allocations can outlive the normal
// closing window. The radio-off proof still precedes any network restart.
#ifndef CSP_ENABLE_BLE_EXTENDED_SETTLEMENT
#define CSP_ENABLE_BLE_EXTENDED_SETTLEMENT 0
#endif
#if CSP_ENABLE_BLE_EXTENDED_SETTLEMENT != 0 && CSP_ENABLE_BLE_EXTENDED_SETTLEMENT != 1
#error CSP_ENABLE_BLE_EXTENDED_SETTLEMENT must be 0 or 1
#endif
#if CSP_ENABLE_BLE_EXTENDED_SETTLEMENT && (!CSP_ENABLE_BLE_RELEASE_SETTLEMENT || !CSP_ENABLE_BACKUP_SETUP)
#error Extended settlement requires the combined verified-release candidate
#endif

// The new Wi-Fi candidate tries less-failed saved profiles before retrying.
// Historical build profiles retain their existing reconnect order.
#ifndef CSP_ENABLE_WIFI_FAIR_RETRY
#define CSP_ENABLE_WIFI_FAIR_RETRY 0
#endif
#if CSP_ENABLE_WIFI_FAIR_RETRY != 0 && CSP_ENABLE_WIFI_FAIR_RETRY != 1
#error CSP_ENABLE_WIFI_FAIR_RETRY must be 0 or 1
#endif

// Private connection-diagnostic candidate. Historical profiles retain their
// scan timing and expose no additional USB reports.
#ifndef CSP_ENABLE_WIFI_ATTEMPT_TRACE
#define CSP_ENABLE_WIFI_ATTEMPT_TRACE 0
#endif
#ifndef CSP_ENABLE_WIFI_STANDARD_SCAN
#define CSP_ENABLE_WIFI_STANDARD_SCAN 0
#endif
#if (CSP_ENABLE_WIFI_ATTEMPT_TRACE != 0 && CSP_ENABLE_WIFI_ATTEMPT_TRACE != 1) || (CSP_ENABLE_WIFI_STANDARD_SCAN != 0 && CSP_ENABLE_WIFI_STANDARD_SCAN != 1)
#error Wi-Fi diagnostic flags must be 0 or 1
#endif
#if (CSP_ENABLE_WIFI_ATTEMPT_TRACE || CSP_ENABLE_WIFI_STANDARD_SCAN) && (!CSP_ENABLE_WIFI_SETUP || !CSP_USB_BENCH_AUTOMATION)
#error Wi-Fi diagnostics require the explicit authenticated USB bench candidate
#endif

// Reversible source experiment: no unchanged-SD scan bypass in existing builds.
#ifndef CSP_ENABLE_SCAN_IDLE_GATE
#define CSP_ENABLE_SCAN_IDLE_GATE 0
#endif
#if CSP_ENABLE_SCAN_IDLE_GATE != 0 && CSP_ENABLE_SCAN_IDLE_GATE != 1
#error CSP_ENABLE_SCAN_IDLE_GATE must be 0 or 1
#endif

// The production environment never enables Bluetooth. The separate M1 target is
// a synthetic-credential, read-only source prototype, not a production image.
#ifndef CSP_ENABLE_BLE_MANAGEMENT
#define CSP_ENABLE_BLE_MANAGEMENT 0
#endif
#ifndef CSP_ENABLE_WIFI_SETUP
#define CSP_ENABLE_WIFI_SETUP 0
#endif
#ifndef CSP_USB_BENCH_AUTOMATION
#define CSP_USB_BENCH_AUTOMATION 0
#endif
#ifndef CSP_BENCH_RADIO_RELEASE
#define CSP_BENCH_RADIO_RELEASE 0
#endif
#ifndef CSP_BACKUP_LINK_PROBE
#define CSP_BACKUP_LINK_PROBE 0
#endif
#ifndef CSP_ENABLE_BACKUP_SETUP
#define CSP_ENABLE_BACKUP_SETUP 0
#endif
#ifndef CSP_BACKUP_FIXTURE_ONLY
#define CSP_BACKUP_FIXTURE_ONLY 0
#endif
#if CSP_BACKUP_FIXTURE_ONLY != 0 && CSP_BACKUP_FIXTURE_ONLY != 1
#error CSP_BACKUP_FIXTURE_ONLY must be 0 or 1
#endif
#if CSP_BACKUP_FIXTURE_ONLY && (!CSP_ENABLE_BACKUP_SETUP || !CSP_USB_BENCH_AUTOMATION)
#error Synthetic backup fixture target requires the explicit combined USB bench
#endif
#if CSP_ENABLE_BACKUP_SETUP != 0 && CSP_ENABLE_BACKUP_SETUP != 1
#error CSP_ENABLE_BACKUP_SETUP must be 0 or 1
#endif
#if CSP_ENABLE_BACKUP_SETUP && (!CSP_ENABLE_WIFI_SETUP || CSP_BACKUP_LINK_PROBE || CSP_BLE_ALLOC_TRACE)
#error Combined backup setup requires its owner-authenticated Wi-Fi build, without link probe or allocation tracing
#endif
// The combined target uses the verified release/drain sequence while retaining
// the default-off production build and the SD-absent experiment's admission.
#define CSP_RADIO_RELEASE_ENABLED (CSP_BENCH_RADIO_RELEASE || CSP_ENABLE_BACKUP_SETUP)
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE && !CSP_ENABLE_BACKUP_SETUP
#error NVS cache release requires the reviewed combined owner-loop teardown
#endif
#if CSP_NVS_CACHE_LAB && (!CSP_ENABLE_BLE_NVS_CACHE_RELEASE || !CSP_USB_BENCH_AUTOMATION || CSP_BACKUP_FIXTURE_ONLY)
#error Cache lab requires its explicit non-fixture service image and USB bench
#endif
#if CSP_BACKUP_LINK_PROBE != 0 && CSP_BACKUP_LINK_PROBE != 1
#error CSP_BACKUP_LINK_PROBE must be 0 or 1
#endif
#if CSP_BACKUP_LINK_PROBE && (!CSP_USB_BENCH_AUTOMATION || !CSP_BENCH_RADIO_RELEASE)
#error Backup link probe requires the explicit SD-absent radio bench
#endif
#if CSP_BENCH_RADIO_RELEASE != 0 && CSP_BENCH_RADIO_RELEASE != 1
#error CSP_BENCH_RADIO_RELEASE must be 0 or 1
#endif
#if CSP_BENCH_RADIO_RELEASE && !CSP_USB_BENCH_AUTOMATION
#error Radio release experiment requires the explicit SD-absent USB bench
#endif
#if CSP_USB_BENCH_AUTOMATION != 0 && CSP_USB_BENCH_AUTOMATION != 1
#error CSP_USB_BENCH_AUTOMATION must be 0 or 1
#endif
#if CSP_USB_BENCH_AUTOMATION && !CSP_ENABLE_WIFI_SETUP
#error USB bench automation requires the separate owner-authenticated Wi-Fi test build
#endif
#if CSP_ENABLE_WIFI_SETUP != 0 && CSP_ENABLE_WIFI_SETUP != 1
#error CSP_ENABLE_WIFI_SETUP must be 0 or 1
#endif
#ifndef CSP_BLE_ISOLATED_TEST
#define CSP_BLE_ISOLATED_TEST 0
#endif
#ifndef CSP_BLE_ALLOC_TRACE
#define CSP_BLE_ALLOC_TRACE 0
#endif
#if CSP_BLE_ALLOC_TRACE != 0 && CSP_BLE_ALLOC_TRACE != 1
#error CSP_BLE_ALLOC_TRACE must be 0 or 1
#endif
#if CSP_BLE_ALLOC_TRACE && !CSP_BLE_ISOLATED_TEST && !CSP_USB_BENCH_AUTOMATION
#error Allocation tracing requires an explicit isolated or SD-absent USB bench test
#endif
#if CSP_ENABLE_BLE_MANAGEMENT != 0 && CSP_ENABLE_BLE_MANAGEMENT != 1
#error CSP_ENABLE_BLE_MANAGEMENT must be 0 or 1
#endif
#if CSP_ENABLE_BLE_MANAGEMENT && !CSP_ENABLE_WIFI_SETUP && !defined(CSP_BLE_SYNTHETIC_ONLY)
#error Milestone 1 BLE requires the explicit synthetic-only build flag
#endif
#if CSP_ENABLE_BLE_MANAGEMENT && !CSP_ENABLE_WIFI_SETUP && CSP_BLE_ISOLATED_TEST != 1
#error Milestone 1 BLE must use the isolated test environment
#endif
#if CSP_ENABLE_WIFI_SETUP && (!CSP_ENABLE_BLE_MANAGEMENT || CSP_BLE_ISOLATED_TEST || (CSP_BLE_ALLOC_TRACE && !CSP_USB_BENCH_AUTOMATION))
#error Wi-Fi setup requires its separate device-owner build, not the M1/trace image
#endif
#if CSP_ENABLE_WIFI_SETUP && defined(CSP_BLE_SYNTHETIC_ONLY)
#error Public M1 credentials may never authorize real Wi-Fi setup
#endif
#if CSP_BLE_ISOLATED_TEST && !CSP_ENABLE_BLE_MANAGEMENT
#error Isolated BLE test requires Bluetooth management
#endif
