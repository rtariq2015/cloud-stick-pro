#pragma once
#include "owner_login_feature.h"
#if CSP_ENABLE_OWNER_LOGIN
#include <wifi_profile_store.h>
class OwnerLoginNvs final:public pro::wifi_setup::BlobStore {
 public:
  pro::wifi_setup::Read read(const char*,uint8_t*,size_t,size_t&)override;
  bool write(const char*,const uint8_t*,size_t)override;
};
#endif
