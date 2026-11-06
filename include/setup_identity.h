#pragma once
#include <Arduino.h>

// Load the stable setup AP/admin identity from its own NVS namespace. Only a
// missing identity is generated; malformed data or storage failures return false
// without changing the output strings or silently replacing saved credentials.
// Call from the portal/main task after Wi-Fi starts, so the hardware RNG has RF
// entropy. Does not initialize, erase, or repair NVS, and never logs credentials.
bool load_setup_identity(String& ssid, String& password);
