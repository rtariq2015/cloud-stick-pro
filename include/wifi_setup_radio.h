#pragma once
#include "ble_feature.h"
#if CSP_ENABLE_WIFI_SETUP
#include <wifi_setup_model.h>
enum class WifiSetupRadioState:uint8_t {Idle,Scanning,Cancelling,Connecting,LocalConnected,Failed,Fault};
enum class WifiSetupRadioStep:uint8_t {None,Mode,Storage,Station,WifiEvents,IpEvents,BarrierEvents,
  Stop,BarrierPost,BarrierWait,DhcpStop,ClearAddress,Start,Configure,DhcpStart,Ready,
  ClearScanList,StopScan,StartScan,ScanCount,ScanRecords,Connect,Unattributed,
  ReleaseStart,ReleaseMode,ReleaseProof,ReleaseWifiHandler,ReleaseIpHandler,ReleaseBarrierHandler};
struct WifiSetupScanRow {char ssid[33]{};int8_t rssi=0;uint8_t channel=0;bool open=false,supported=false;
#if CSP_ENABLE_GUEST_DIAGNOSTIC
  uint8_t authmode=0;
#endif
};
struct WifiSetupRadioSnapshot {
  WifiSetupRadioState state=WifiSetupRadioState::Idle;
  pro::wifi_setup::Error error=pro::wifi_setup::Error::None;
  uint32_t profile=0;
  WifiSetupRadioStep step=WifiSetupRadioStep::None;
  int32_t sdk_error=0;
  uint8_t count=0;bool truncated=false,has_scan=false;
#if CSP_ENABLE_GUEST_DIAGNOSTIC
  int disconnect_reason=0;
#endif
  WifiSetupScanRow rows[20]{};
};
bool wifi_setup_radio_init();
bool wifi_setup_radio_scan();
#if CSP_ENABLE_GUEST_DIAGNOSTIC
bool wifi_setup_radio_scan(const char* target);
#endif
bool wifi_setup_radio_connect(const pro::wifi_setup::CheckProfile&);
bool wifi_setup_radio_disconnect();
#if CSP_RADIO_RELEASE_ENABLED
bool wifi_setup_radio_release();
#endif
void wifi_setup_radio_poll();
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot();
#endif
