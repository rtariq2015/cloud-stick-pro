#pragma once
#include "wifi_setup_model.h"
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
#include "owner_cloud_feature.h"
#include "../owner_cloud_setup/owner_cloud_model.h"
#endif
#if defined(CSP_ENABLE_OWNER_LOGIN) && CSP_ENABLE_OWNER_LOGIN
#include "owner_login_feature.h"
#include "../owner_login/owner_login_model.h"
#endif
namespace pro::wifi_setup {
constexpr size_t WifiWireLimit=496,WifiResultLimit=400;
enum class Op:uint8_t {List,Scan,ScanPage,Begin,Stage,Forget,Check,Status,Commit,Abort,Result,BackupStatus
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
  ,CloudStatus,CloudBegin,CloudStage,CloudCommit,CloudAbort
#endif
#if defined(CSP_ENABLE_OWNER_LOGIN) && CSP_ENABLE_OWNER_LOGIN
  ,LoginBegin,LoginStage,LoginCommit,LoginAbort
#endif
#if defined(CSP_ENABLE_FILE_HISTORY) && CSP_ENABLE_FILE_HISTORY && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  ,HistoryStatus,HistoryRow
#endif
#if defined(CSP_ENABLE_EVENT_LOG) && CSP_ENABLE_EVENT_LOG && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  ,EventLog
#endif
#if defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  ,PortalBegin,PortalHandoff,PortalStatus,PortalCancel
#endif
};
struct WireRequest {
  uint32_t id=0,profile=0,cursor=0,job=0,edit=0;
  uint64_t revision=0,transaction=0;
#if defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  uint64_t grant=0;
#endif
  Op op=Op::Status;
  std::array<char,33> ssid{};
  std::array<char,64> secret{};
  uint8_t priority=0;
  bool enabled=true,open=false,keep=false,confirm=false,accept_local=false;
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
  pro::owner_cloud::Field cloud_field=pro::owner_cloud::Field::Username;
  std::array<char,129> cloud_value{};
  bool accept_unverified=false;
#endif
#if defined(CSP_ENABLE_OWNER_LOGIN) && CSP_ENABLE_OWNER_LOGIN
  std::array<uint8_t,pro::owner_login::ChunkLimit> login_bytes{};
  uint16_t login_size=0;
#endif
  ~WireRequest(){eraseSecretMemory(secret.data(),secret.size());
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
    eraseSecretMemory(cloud_value.data(),cloud_value.size());
#endif
#if defined(CSP_ENABLE_OWNER_LOGIN) && CSP_ENABLE_OWNER_LOGIN
    eraseSecretMemory(login_bytes.data(),login_bytes.size());
#endif
  }
};
bool parseWire(const uint8_t*,size_t,WireRequest&);
} // namespace pro::wifi_setup
