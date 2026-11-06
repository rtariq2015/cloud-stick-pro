#include "wifi_scan.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace {
// The IDF event task only owns eventResults/sdkRecords between the atomic
// Running->Collecting->Complete transition. HTTP/main only reads eventResults
// after Complete (acquire), so no SDK calls or large copies occur in a spinlock.
enum class Phase : uint8_t { Idle, Running, Collecting, Complete, Cancelling, Cancelled };
std::atomic<Phase> phase{Phase::Idle};
esp_event_handler_instance_t handler = nullptr;
// Static SDK scratch avoids putting 20 large wifi_ap_record_t entries on the
// small IDF event-loop stack. This is the sole scanner and one scan can run.
wifi_ap_record_t sdkRecords[WifiScanLimit] = {};
WifiScanSnapshot eventResults;
WifiScanSnapshot cache;
uint32_t lastAttempt = 0, scanStarted = 0, cachedAt = 0;
bool attempted = false;

const char* stateName(WifiScanState state) {
  switch (state) {
    case WifiScanState::Scanning: return "scanning";
    case WifiScanState::Ready: return "ready";
    case WifiScanState::Error: return "error";
    default: return "idle";
  }
}

// SSIDs are opaque 802.11 bytes, but this browser configuration accepts printable
// UTF-8. Skip hidden/binary/control-byte SSIDs instead of creating malformed JSON
// or silently selecting a different name. Manual SSID entry remains available.
bool browserSSID(const uint8_t* ssid, size_t& length) {
  length = strnlen(reinterpret_cast<const char*>(ssid), 33);
  if (!length || length > 32) return false;
  for (size_t i = 0; i < length; ++i) {
    uint8_t c = ssid[i];
    if (c < 32 || c == 127) return false;
    if (c < 128) continue;
    size_t more;
    uint32_t value;
    if (c >= 0xc2 && c <= 0xdf) { more = 1; value = c & 31; }
    else if (c >= 0xe0 && c <= 0xef) { more = 2; value = c & 15; }
    else if (c >= 0xf0 && c <= 0xf4) { more = 3; value = c & 7; }
    else return false;
    if (i + more >= length) return false;
    for (size_t k = 1; k <= more; ++k) {
      uint8_t next = ssid[i + k];
      if ((next & 0xc0) != 0x80) return false;
      value = (value << 6) | (next & 63);
    }
    if ((more == 2 && value < 0x800) || (more == 3 && value < 0x10000) ||
        value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    i += more;
  }
  return true;
}

bool personalAuth(wifi_auth_mode_t auth) {
  return auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_WPA2_PSK || auth == WIFI_AUTH_WPA_WPA2_PSK ||
         auth == WIFI_AUTH_WPA3_PSK || auth == WIFI_AUTH_WPA2_WPA3_PSK;
}

void scanDone(void*, esp_event_base_t base, int32_t id, void* payload) {
  if (base != WIFI_EVENT || id != WIFI_EVENT_SCAN_DONE) return;
  Phase expected = Phase::Running;
  if (!phase.compare_exchange_strong(expected, Phase::Collecting)) {
    if (expected == Phase::Cancelling) {
      esp_wifi_clear_ap_list();
      phase.store(Phase::Cancelled, std::memory_order_release);
    }
    return;
  }
  eventResults.state = WifiScanState::Error;
  eventResults.error = 0;
  eventResults.count = 0;
  eventResults.has_cache = false;
  eventResults.truncated = false;
  const auto* result = static_cast<const wifi_event_sta_scan_done_t*>(payload);
  eventResults.driver_status = result ? result->status : 1;
  esp_err_t error = eventResults.driver_status == 0 ? ESP_OK : ESP_FAIL;
  uint16_t total = 0, count = WifiScanLimit;
  if (error == ESP_OK) error = esp_wifi_scan_get_ap_num(&total);
  if (error == ESP_OK && total) {
    // IDF copies at most 20 strongest results and frees its entire AP list.
    error = esp_wifi_scan_get_ap_records(&count, sdkRecords);
    count = std::min<uint16_t>(count, WifiScanLimit);
  } else count = 0;
  // Also release driver allocations on zero results or get-records failure.
  esp_wifi_clear_ap_list();
  eventResults.error = error;
  eventResults.total_found = total;
  eventResults.truncated = total > WifiScanLimit;
  if (error == ESP_OK) {
    eventResults.state = WifiScanState::Ready;
    eventResults.has_cache = true;
    for (uint16_t i = 0; i < count; ++i) {
      size_t length = 0;
      if (!browserSSID(sdkRecords[i].ssid, length)) continue;
      bool duplicate = false;
      for (size_t j = 0; j < eventResults.count; ++j) {
        if (eventResults.networks[j].auth == uint8_t(sdkRecords[i].authmode) &&
            strcmp(eventResults.networks[j].ssid,
                   reinterpret_cast<const char*>(sdkRecords[i].ssid)) == 0) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      auto& network = eventResults.networks[eventResults.count++];
      memcpy(network.ssid, sdkRecords[i].ssid, length);
      network.ssid[length] = '\0';
      network.rssi = sdkRecords[i].rssi;
      network.channel = sdkRecords[i].primary;
      network.auth = uint8_t(sdkRecords[i].authmode);
      network.secure = sdkRecords[i].authmode != WIFI_AUTH_OPEN;
      network.personal = personalAuth(sdkRecords[i].authmode);
    }
  }
  memset(sdkRecords, 0, sizeof sdkRecords);
  phase.store(Phase::Complete, std::memory_order_release);
}

void setError(esp_err_t error) {
  // Preserve any previous bounded cache for manual selection on transient error.
  cache.state = WifiScanState::Error;
  cache.error = error;
  cache.driver_status = 0;
}

void cancel(esp_err_t reason) {
  Phase expected = Phase::Running;
  if (!phase.compare_exchange_strong(expected, Phase::Cancelling)) return;
  // A scan explicitly stopped by IDF produces SCAN_DONE. Until that event is
  // drained, reject new starts so a stale completion cannot complete a new scan.
  // If the event never arrives, manual entry/reboot remains available.
  esp_wifi_scan_stop();
  esp_wifi_clear_ap_list();
  setError(reason);
}
}

void wifi_scan_poll() {
  Phase current = phase.load(std::memory_order_acquire);
  if (current == Phase::Complete) {
    if (eventResults.state == WifiScanState::Ready) {
      cache = eventResults;
      cachedAt = millis();
    } else {
      setError(eventResults.error);
      cache.driver_status = eventResults.driver_status;
    }
    phase.store(Phase::Idle, std::memory_order_release);
  } else if (current == Phase::Cancelled) {
    phase.store(Phase::Idle, std::memory_order_release);
  } else if (current == Phase::Running && millis() - scanStarted >= WifiScanTimeoutMs) {
    cancel(ESP_ERR_TIMEOUT);
  }
}

bool wifi_scan_request() {
  wifi_scan_poll();
  Phase current = phase.load(std::memory_order_acquire);
  if (current == Phase::Running || current == Phase::Collecting) return true;
  if (current != Phase::Idle) return false;
  uint32_t now = millis();
  if (attempted && now - lastAttempt < WifiScanIntervalMs) return false;
  attempted = true;
  lastAttempt = now;

  wifi_mode_t mode;
  esp_err_t error = esp_wifi_get_mode(&mode);
  if (error != ESP_OK || (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)) {
    setError(error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
    return false;
  }
  WiFi.setAutoReconnect(false);
  // This adds only an unassociated station; WiFi.mode does not call connect.
  // The existing AP netif, credentials, address and listener remain in place.
  if (mode == WIFI_MODE_AP && !WiFi.mode(WIFI_AP_STA)) {
    setError(ESP_FAIL);
    return false;
  }
  wifi_ap_record_t associated;
  if (esp_wifi_sta_get_ap_info(&associated) == ESP_OK) {
    setError(ESP_ERR_INVALID_STATE);
    return false; // This API is maintenance-only, never a connected-STA scanner.
  }
  if (!handler) {
    error = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                               scanDone, nullptr, &handler);
    if (error != ESP_OK) { setError(error); return false; }
  }
  esp_wifi_clear_ap_list();
  wifi_scan_config_t scan = {};
  scan.channel = 0;
  scan.show_hidden = false;
  scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan.scan_time.active.min = 30;
  scan.scan_time.active.max = 80;
  scan.home_chan_dwell_time = 30;
  cache.state = WifiScanState::Scanning;
  cache.error = 0;
  cache.driver_status = 0;
  scanStarted = now;
  phase.store(Phase::Running, std::memory_order_release);
  error = esp_wifi_scan_start(&scan, false);
  if (error != ESP_OK) {
    phase.store(Phase::Idle, std::memory_order_release);
    esp_wifi_clear_ap_list();
    setError(error);
    return false;
  }
  return true;
}

WifiScanSnapshot wifi_scan_snapshot() {
  WifiScanSnapshot result = cache;
  uint32_t now = millis();
  uint32_t elapsed = now - lastAttempt;
  result.retry_after_ms = attempted && elapsed < WifiScanIntervalMs
                            ? WifiScanIntervalMs - elapsed : 0;
  result.cache_age_ms = result.has_cache ? now - cachedAt : 0;
  return result;
}

String wifi_scan_json() {
  wifi_scan_poll();
  WifiScanSnapshot snapshot = wifi_scan_snapshot();
  JsonDocument doc;
  doc["state"] = stateName(snapshot.state);
  doc["error"] = snapshot.error;
  doc["message"] = snapshot.error == ESP_ERR_TIMEOUT ? "Scan timed out; enter the Wi-Fi name manually" :
                   snapshot.error ? "Wi-Fi scan unavailable; manual entry is available" :
                   snapshot.state == WifiScanState::Scanning ? "Scanning nearby Wi-Fi networks" :
                   snapshot.state == WifiScanState::Ready ? "Choose your authorized Wi-Fi network" : "Scan not started";
  doc["retry_after_ms"] = snapshot.retry_after_ms;
  doc["cache_age_ms"] = snapshot.cache_age_ms;
  doc["has_cache"] = snapshot.has_cache;
  doc["truncated"] = snapshot.truncated;
  doc["total_found"] = snapshot.total_found;
  doc["driver_status"] = snapshot.driver_status;
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (size_t i = 0; i < snapshot.count; ++i) {
    const auto& network = snapshot.networks[i];
    JsonObject row = networks.add<JsonObject>();
    row["ssid"] = network.ssid;
    row["rssi"] = network.rssi;
    row["channel"] = network.channel;
    row["secure"] = network.secure;
    row["personal"] = network.personal;
    row["auth"] = network.auth;
  }
  // At most 20 SSIDs of at most 32 UTF-8 bytes; keep the response bounded.
  if (doc.overflowed() || measureJson(doc) > 4096) return F("{\"state\":\"error\",\"error\":257,\"message\":\"Scan response unavailable\",\"networks\":[]}");
  String out;
  out.reserve(measureJson(doc) + 1);
  if (!serializeJson(doc, out)) return F("{\"state\":\"error\",\"error\":257,\"networks\":[]}");
  return out;
}

void wifi_scan_cancel() {
  wifi_scan_poll();
  cancel(ESP_ERR_INVALID_STATE);
}

bool wifi_scan_busy() {
  wifi_scan_poll();
  return phase.load(std::memory_order_acquire) != Phase::Idle;
}
