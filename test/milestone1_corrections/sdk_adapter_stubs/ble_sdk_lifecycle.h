#pragma once
#include "common.h"
inline esp_err_t csp_ble_sdk_start(protocomm_t* pc,const protocomm_ble_config_t* c){return protocomm_ble_start(pc,c);}
inline esp_err_t csp_ble_sdk_stop(protocomm_t* pc){return protocomm_ble_stop(pc);}
inline bool csp_ble_sdk_idle(){return simulatedStopOk;}
