#include "setup_identity.h"
#include <esp_random.h>
#include <nvs.h>
#include <cstring>
#include <utility>

namespace {
constexpr char Namespace[] = "csp-setup";
constexpr char Key[] = "identity";
constexpr char Prefix[] = "CloudStickPro-";
constexpr size_t SuffixLength = 6;
constexpr size_t PasswordLength = 16;
constexpr uint8_t Schema = 1;

// All fields have byte alignment, so this schema has no padding or endian
// dependency. Store one small blob so SSID/password cannot be updated apart.
struct StoredIdentity {
  uint8_t schema;
  char suffix[SuffixLength + 1];
  char password[PasswordLength + 1];
};
static_assert(sizeof(StoredIdentity) == 25, "Unexpected setup identity layout");

bool fixedHex(const char* value, size_t length) {
  if (value[length] != '\0') return false;
  for (size_t i = 0; i < length; ++i) {
    if (!((value[i] >= '0' && value[i] <= '9') ||
          (value[i] >= 'a' && value[i] <= 'f'))) return false;
  }
  return true;
}

bool readIdentity(nvs_handle_t handle, StoredIdentity& identity, bool& absent) {
  absent = false;
  size_t size = 0;
  esp_err_t error = nvs_get_blob(handle, Key, nullptr, &size);
  if (error == ESP_ERR_NVS_NOT_FOUND) { absent = true; return true; }
  if (error != ESP_OK || size != sizeof identity) return false;
  error = nvs_get_blob(handle, Key, &identity, &size);
  return error == ESP_OK && size == sizeof identity &&
         identity.schema == Schema && fixedHex(identity.suffix, SuffixLength) &&
         fixedHex(identity.password, PasswordLength);
}

void generateIdentity(StoredIdentity& identity) {
  constexpr char Hex[] = "0123456789abcdef";
  uint8_t random[11];
  esp_fill_random(random, sizeof random);
  identity.schema = Schema;
  for (size_t i = 0; i < 3; ++i) {
    identity.suffix[i * 2] = Hex[random[i] >> 4];
    identity.suffix[i * 2 + 1] = Hex[random[i] & 15];
  }
  identity.suffix[SuffixLength] = '\0';
  for (size_t i = 0; i < 8; ++i) {
    identity.password[i * 2] = Hex[random[i + 3] >> 4];
    identity.password[i * 2 + 1] = Hex[random[i + 3] & 15];
  }
  identity.password[PasswordLength] = '\0';
}
}

bool load_setup_identity(String& ssid, String& password) {
  nvs_handle_t handle;
  if (nvs_open(Namespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  StoredIdentity identity{};
  bool absent = false;
  if (!readIdentity(handle, identity, absent)) { nvs_close(handle); return false; }
  if (absent) {
    generateIdentity(identity);
    bool saved = nvs_set_blob(handle, Key, &identity, sizeof identity) == ESP_OK &&
                 nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (!saved) return false;
    // Reopen and verify the committed blob before exposing this identity.
    if (nvs_open(Namespace, NVS_READONLY, &handle) != ESP_OK) return false;
    StoredIdentity verified{};
    bool read = readIdentity(handle, verified, absent);
    nvs_close(handle);
    if (!read || absent || memcmp(&identity, &verified, sizeof identity) != 0) return false;
  } else {
    nvs_close(handle);
  }
  String newSSID(Prefix);
  newSSID += identity.suffix;
  String newPassword(identity.password);
  if (newSSID.length() != sizeof Prefix - 1 + SuffixLength ||
      newPassword.length() != PasswordLength) return false;
  ssid = std::move(newSSID);
  password = std::move(newPassword);
  return true;
}
