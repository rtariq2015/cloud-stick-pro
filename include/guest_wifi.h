#pragma once
#include <Arduino.h>

// Maintenance-only, portal/main-task API. The caller provides authentication,
// CSRF protection and exclusive access against Wi-Fi scanning/backups.
// Keeps the protected AP and its listener intact; the single radio follows the
// upstream channel, so a phone may briefly reconnect. Credentials stay in RAM.
bool guest_wifi_connect(const String& ssid, const String& password, String& error);
void guest_wifi_poll();
// state: idle/connecting/connected/error, plus a bounded, credential-free message.
// Connected means IPv4/NAPT/DNS are ready; it does not verify Internet access.
String guest_wifi_json();
// Disable forwarding/DNS and disconnect only STA, preserving the setup AP.
void guest_wifi_stop();
bool guest_wifi_active();
struct CloudConfig;
// Reuse the active connection's credentials on-device, without returning secrets
// to a browser after reconnecting to a moved setup address.
bool guest_wifi_fill_config(CloudConfig& config);
// Notify the browser before moving the setup interface off a conflicting subnet.
void guest_wifi_relocation_sent();
uint32_t guest_wifi_relocation_ready();
void guest_wifi_relocated(bool ok);
