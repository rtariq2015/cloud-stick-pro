#pragma once
#include "ble_feature.h"
#if CSP_ENABLE_GUEST_DIAGNOSTIC
#include <cstdint>
namespace pro::guest_diag {
struct InternetResult {
  bool dns_http=false,dns_https=false,clock_valid=false,https_verified=false;
  int http_status=0,https_status=0,http_error=0,https_error=0,tls_error=0;
};
// Owner-loop admission only, after worker/BLE/listener retirement. No files,
// cloud configuration, bodies, cookies, redirects or persistent state are used.
bool internetStart();
void internetCancel();
bool internetBusy();
bool internetReap(InternetResult&);
}
#endif
