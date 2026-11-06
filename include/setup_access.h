#pragma once

// Fixed setup credentials requested for this personal build. These are separate
// from the saved upstream Wi-Fi and cloud credentials. Reboots never rotate them.
namespace setup_access {
constexpr char Username[] = "admin";
constexpr char WebPassword[] = "admin";
constexpr char WifiPassword[] = "admin123";
static_assert(sizeof(WifiPassword) - 1 >= 8 && sizeof(WifiPassword) - 1 <= 63,
              "WPA2 passphrase must contain 8 through 63 characters");
}
