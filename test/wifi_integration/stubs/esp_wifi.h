#pragma once
#include "common.h"
enum wifi_ps_type_t {WIFI_PS_NONE=0,WIFI_PS_MIN_MODEM=1,WIFI_PS_MAX_MODEM=2};
namespace power_save_fixture {
inline unsigned psSetCalls=0,psGetCalls=0;
inline int psSetError=ESP_OK,psGetError=ESP_OK;
inline bool psApply=true;
inline wifi_ps_type_t observedPs=WIFI_PS_MIN_MODEM;
inline std::vector<int> psCalls;
}
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t value){
  using namespace power_save_fixture;
  ++psSetCalls;psCalls.push_back(100+int(value));
  if(psSetError==ESP_OK&&psApply)observedPs=value;
  return psSetError;
}
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* value){
  using namespace power_save_fixture;
  ++psGetCalls;psCalls.push_back(200);
  if(psGetError==ESP_OK)*value=observedPs;
  return psGetError;
}
