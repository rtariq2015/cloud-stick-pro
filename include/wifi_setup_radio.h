#pragma once
#include "ble_feature.h"
#if CSP_ENABLE_WIFI_SETUP
#include <wifi_setup_model.h>
enum class WifiSetupRadioState:uint8_t {Idle,Scanning,Cancelling,Connecting,LocalConnected,Failed,Fault};
enum class WifiSetupRadioStep:uint8_t {None,Mode,Storage,Station,WifiEvents,IpEvents,BarrierEvents,
  Stop,BarrierPost,BarrierWait,DhcpStop,ClearAddress,Start,Configure,DhcpStart,Ready,
  ClearScanList,StopScan,StartScan,ScanCount,ScanRecords,Connect,Unattributed,
  ReleaseStart,ReleaseMode,ReleaseProof,ReleaseWifiHandler,ReleaseIpHandler,ReleaseBarrierHandler};
struct WifiSetupScanRow {char ssid[33]{};int8_t rssi=0;uint8_t channel=0;bool open=false,supported=false;};
struct WifiSetupRadioSnapshot {
  WifiSetupRadioState state=WifiSetupRadioState::Idle;
  pro::wifi_setup::Error error=pro::wifi_setup::Error::None;
  uint32_t profile=0;
  WifiSetupRadioStep step=WifiSetupRadioStep::None;
  int32_t sdk_error=0;
  uint8_t count=0;bool truncated=false,has_scan=false;
  WifiSetupScanRow rows[20]{};
};
#if CSP_ENABLE_WIFI_ATTEMPT_TRACE
// Numeric metadata only: no SSID, address, password, payload or SDK pointer.
enum WifiAttemptFlag:uint8_t {WifiAttemptGotIP=1,WifiAttemptAssociated=2,
  WifiAttemptNameMatches=4,WifiAttemptAuthSupported=8,WifiAttemptOpenMatches=16,
  WifiAttemptLeaseValid=32,WifiAttemptLostIP=64};
enum class WifiAttemptOutcome:uint8_t {Empty,Pending,Connected,Failed,Cancelled};
struct WifiSetupAttempt {
  uint32_t sequence=0,elapsed_ms=0;
  WifiSetupRadioStep step=WifiSetupRadioStep::None;
  int32_t sdk_error=0,disconnect_reason=0;
  pro::wifi_setup::Error error=pro::wifi_setup::Error::None;
  WifiAttemptOutcome outcome=WifiAttemptOutcome::Empty;
  uint8_t flags=0;
};
const WifiSetupAttempt& wifi_setup_radio_attempt();
const WifiSetupAttempt& wifi_setup_radio_last_failure();
#endif
bool wifi_setup_radio_init();
bool wifi_setup_radio_scan();
bool wifi_setup_radio_connect(const pro::wifi_setup::CheckProfile&);
bool wifi_setup_radio_disconnect();
#if CSP_RADIO_RELEASE_ENABLED
bool wifi_setup_radio_release();
#endif
void wifi_setup_radio_poll();
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot();
// Owner-loop only: consume synchronously before the next radio operation.
// Nonempty only after the associated AP name/security and DHCP lease are proven;
// the returned storage is owned by the radio and must not be retained by tasks.
const char* wifi_setup_radio_connected_ssid();
#endif
