#pragma once
#include "ble_feature.h"
#if CSP_ENABLE_WIFI_SETUP
#include <wifi_profile_store.h>
#include <owner_record.h>
class WifiNvsStore final:public pro::wifi_setup::BlobStore {
 public:
  pro::wifi_setup::Read read(const char*,uint8_t*,size_t,size_t&) override;
  bool write(const char*,const uint8_t*,size_t) override;
};
enum class WifiOwnerLoad {Ready,Missing,Fault};
enum class WifiOwnerInstallStep:uint8_t {Never,ExistingOwner,Identity,Record,Write,Readback,Verified};
WifiOwnerInstallStep wifi_owner_install_step();
WifiOwnerLoad wifi_owner_load(pro::wifi_setup::OwnerRecord&);
// First enrollment only. Caller separately proves the 10-second physical hold,
// BLE off and no active network attempt. Existing/corrupt keys cannot be replaced.
bool wifi_owner_first_install(const uint8_t*,size_t);
pro::wifi_setup::Stored wifi_import_legacy_network(pro::wifi_setup::StoredNetworks&);
#endif
