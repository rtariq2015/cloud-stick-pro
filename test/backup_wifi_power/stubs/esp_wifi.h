#pragma once
// Reuse checked SDK fixtures while recording ownership/order at every actual
// runtime call. The wrapper cannot change a policy while a worker or BLE lives.
#define esp_wifi_set_ps underlyingFixtureSetPs
#define esp_wifi_get_ps underlyingFixtureGetPs
#include "../../wifi_integration/stubs/esp_wifi.h"
#undef esp_wifi_set_ps
#undef esp_wifi_get_ps
#include "backup_worker.h"
#include <cassert>
extern BackupWorkerSnapshot managed;
extern bool bleRunning;
extern std::vector<std::string> events;
namespace backup_power_fixture {
inline unsigned failReadAt=0;
}
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t mode){
  assert(!bleRunning&&managed.phase==BackupWorkerPhase::Stopped);
  events.emplace_back("ps-write-"+std::to_string(int(mode)));
  return underlyingFixtureSetPs(mode);
}
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* mode){
  assert(!bleRunning);
  using namespace power_save_fixture;
  const int savedError=psGetError;
  if(backup_power_fixture::failReadAt==psGetCalls+1)psGetError=ESP_FAIL;
  const auto result=underlyingFixtureGetPs(mode);psGetError=savedError;
  events.emplace_back(result==ESP_OK?"ps-read-"+std::to_string(int(*mode)):"ps-read-error");
  return result;
}
