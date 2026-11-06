#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Maintenance-only scanner. Call these functions from the portal/main task,
// never from the backup task. They do not connect the station to any network.
// A request requires an existing AP; it enables an unassociated STA alongside
// that AP because ESP-IDF cannot scan in AP-only mode. The AP remains enabled,
// but off-channel scans can briefly increase latency on its single radio.
// The exported/cache limit is 20; the Wi-Fi driver temporarily owns its full
// scan list until completion, when this module retrieves/frees it immediately.
// Do not mix this module with Arduino WiFi.scanNetworks()/scanDelete().
constexpr size_t WifiScanLimit = 20;
constexpr uint32_t WifiScanIntervalMs = 15000;
constexpr uint32_t WifiScanTimeoutMs = 10000;

enum class WifiScanState : uint8_t { Idle, Scanning, Ready, Error };
struct WifiScanNetwork {
  char ssid[33] = {};
  int8_t rssi = 0;
  uint8_t channel = 0;
  bool secure = false;
  bool personal = false; // Selectable: open or WPA2/WPA3 personal; no WEP/enterprise.
  uint8_t auth = 0;      // ESP-IDF wifi_auth_mode_t value, for diagnostics only.
};
struct WifiScanSnapshot {
  WifiScanState state = WifiScanState::Idle;
  int32_t error = 0;
  uint32_t driver_status = 0;
  uint32_t retry_after_ms = 0;
  uint32_t cache_age_ms = 0;
  uint16_t total_found = 0;
  uint8_t count = 0;
  bool has_cache = false;
  bool truncated = false;
  WifiScanNetwork networks[WifiScanLimit] = {};
};

// true = a new/existing scan is running. false = throttled or unavailable;
// inspect snapshot/JSON for cached results, retry delay, or an error.
bool wifi_scan_request();
// Checks timeout/completion without waiting for a scan. Driver results are
// already retrieved/freed in the IDF completion handler, not retained for HTTP.
void wifi_scan_poll();
WifiScanSnapshot wifi_scan_snapshot();
// Bounded JSON: state, error, message, retry_after_ms, cache_age_ms,
// has_cache, truncated, total_found, driver_status, networks (at most 20).
String wifi_scan_json();
// Stop an outstanding scan before shutting down the maintenance AP. This never
// stops Wi-Fi/AP and never calls esp_wifi_connect or WiFi.begin.
void wifi_scan_cancel();
bool wifi_scan_busy();
