#pragma once
#include "owner_cloud_feature.h"
#if CSP_ENABLE_OWNER_CLOUD_SETUP
#include <wifi_profile_store.h>
class OwnerCloudNvs final:public pro::wifi_setup::BlobStore {
 public:
  pro::wifi_setup::Read read(const char*,uint8_t*,size_t,size_t&)override;
  bool write(const char*,const uint8_t*,size_t)override;
};
#endif
