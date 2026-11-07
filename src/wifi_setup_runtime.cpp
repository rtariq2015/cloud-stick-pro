#include "wifi_setup_runtime.h"
#include "public_wifi_assistance.h"
#include "backup_wifi_power.h"
#include "owner_cloud_feature.h"
#include "owner_login_feature.h"
#if CSP_ENABLE_WIFI_SETUP
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
#include "owned_guest_route.h"
#include "public_wifi_probe.h"
#endif
#include "wifi_setup_radio.h"
#include "wifi_setup_nvs.h"
#include "wifi_setup_nvs_release.h"
#include "wifi_setup_cache_lab.h"
#include "ble_management.h"
#include "ble_platform_prepare.h"
#include "ble_sdk_lifecycle.h"
#include "ble_alloc_trace.h"
#include "pro_ble_lifecycle.h"
#include "pro_ble_message.h"
#include "pro_device.h"
#include "pro_event_log.h"
#include "file_history.h"
#include "upload_diagnostics.h"
#include "panic_diagnostics.h"
#include "local_dashboard.h"
#include "lcd.h"
#include "usb_storage.h"
#if CSP_ENABLE_OWNER_CLOUD_SETUP
#include "owner_cloud_nvs.h"
#include "koofr_ca.h"
#include <owner_cloud_store.h>
#include <memory>
#endif
#if CSP_ENABLE_OWNER_LOGIN
#include "owner_login_nvs.h"
#include <owner_login_store.h>
#include <owner_login_model.h>
#include <memory>
#endif
#if CSP_ENABLE_BACKUP_SETUP
#include "backup_worker.h"
#include "backup_memory.h"
#include "pro_wifi_reconnect.h"
#include <ctime>
#endif
#include <wifi_wire.h>
#include <secure_json.h>
#include <WiFi.h>
#include <USB.h>
#include <USBCDC.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#if CSP_ENABLE_BACKUP_SETUP && (CSP_USB_BENCH_AUTOMATION || CSP_BACKUP_WIFI_AWAKE)
#include <esp_wifi.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace pro::wifi_setup;
namespace {
WifiNvsStore nvs;
ProfileStore store(nvs);
Candidate model;
OwnerRecord owner;
WifiOwnerLoad owner_state=WifiOwnerLoad::Missing;
pro::BleLifecycle lifecycle;
bool store_fault=false,radio_ready=false,check_pending=false;
#if CSP_ENABLE_OWNER_CLOUD_SETUP
namespace cloud=pro::owner_cloud;
OwnerCloudNvs cloud_nvs;
cloud::Store cloud_store(cloud_nvs,KoofrCA);
struct CloudDraft {cloud::Record base;cloud::Candidate candidate;WifiSetupSession session;};
std::unique_ptr<CloudDraft> cloud_draft;
cloud::Info cloud_confirmed;
uint64_t cloud_next_transaction=0;
bool cloud_store_fault=false,cloud_clock_pending=false;
void clearCloud(){cloud_draft.reset();}
#endif
#if CSP_ENABLE_OWNER_LOGIN
namespace login=pro::owner_login;
OwnerLoginNvs login_nvs;
struct LoginDraft {login::Current base;login::Candidate candidate;WifiSetupSession session;};
std::unique_ptr<LoginDraft> login_draft;
login::Handoff login_handoff; // Every access is under mailbox_mutex.
std::atomic<bool> login_barrier{false};
std::mutex login_output_mutex;
uint64_t login_next_transaction=0;
bool login_fault=false,login_close_requested=false;
void clearLogin(){login_draft.reset();}
#endif
#if !CSP_BACKUP_LINK_PROBE && !CSP_ENABLE_BACKUP_SETUP
bool setup_paused=false;
#endif
bool enrollment_wait_release=false;
bool closing=false,close_stop_verified=false;
constexpr uint32_t CloseSettlementMs=30000;
#if CSP_RADIO_RELEASE_ENABLED
bool release_pending=false,restart_attempted=false;
uint32_t release_started=0,release_free=0,release_largest=0,release_wait_ms=0;
#endif
#if CSP_USB_BENCH_AUTOMATION
bool bench_close_queued=false;
#endif
uint32_t close_started=0;
Ticket checking;
WifiSetupSession transaction_session{},check_session{};
uint32_t before_free=0,before_largest=0,last_draw=0,button_down=0;
uint32_t after_free=0,after_largest=0,save_before_free=0,save_before_largest=0,save_after_free=0,save_after_largest=0;
uint32_t last_serial_reply=0;
bool recovery_checked=false,recovery_restored=false;
const char* last_lcd=nullptr;
const char* display="Wi-Fi setup: hold BOOT 3s";
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
pro::public_wifi::Coordinator public_coordinator;
WifiSetupSession public_session;
std::atomic<uint32_t> public_handoff_job{0};
std::atomic<bool> public_result_returned{false};
bool public_close_requested=false,public_probe_issued=false;
char public_ap_name[32]{};
char public_signin_screen[80]{};
bool publicBlocks(){
  return public_coordinator.blocksAutomaticWork()||pro::owned_guest::status().phase!=pro::owned_guest::Phase::Stopped||
    pro::public_wifi::probeSnapshot().phase!=pro::public_wifi::ProbePhase::Stopped;
}
bool publicOwnsStation(){
  const auto phase=public_coordinator.snapshot(millis()).phase;
  return phase==pro::public_wifi::Phase::Routing||phase==pro::public_wifi::Phase::StoppingRoute||
    phase==pro::public_wifi::Phase::WaitingProbe||phase==pro::public_wifi::Phase::Probing||
    phase==pro::public_wifi::Phase::Recovering||phase==pro::public_wifi::Phase::Blocked;
}
#endif
#if CSP_ENABLE_BACKUP_SETUP
uint32_t freeInternal();
uint32_t largestInternal();
bool backup_config_valid=false;
// validate_config caps the NTP hostname at 128 bytes. Pinned lwIP SNTP borrows
// its server-name pointer, so retain only this public value for the app lifetime
// while the temporary cloud credentials/configuration can still be released.
char time_server[129]{};
bool backup_pause_requested=false,backup_bridge_fault=false;
#if CSP_BACKUP_WIFI_AWAKE
pro::BackupWifiPower backup_power;
static_assert(WIFI_PS_NONE==0&&WIFI_PS_MIN_MODEM==1&&WIFI_PS_MAX_MODEM==2,
              "Reviewed SDK power-save mode values changed");
int backupPowerRead(void*,int& value){
  wifi_ps_type_t mode=WIFI_PS_MIN_MODEM;
  const auto code=esp_wifi_get_ps(&mode);
  if(code==ESP_OK)value=int(mode);
  return code;
}
int backupPowerWrite(void*,int value){return esp_wifi_set_ps(static_cast<wifi_ps_type_t>(value));}
const pro::BackupWifiPower::Driver backup_power_driver{nullptr,backupPowerRead,backupPowerWrite};
bool backupPowerFault(){
  backup_bridge_fault=true;set_maintenance(true);
  display="Wi-Fi power needs review\nFiles stay on USB";return false;
}
bool releaseBackupPower(){
  if(backup_power.faulted())return backupPowerFault();
  if(!backup_power.owned())return true;
  // This helper is reached only after the managed task has actually been
  // reaped. Never touch a policy underneath BLE or a surviving worker.
  if(ble_management_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped)return false;
  return backup_power.release(backup_power_driver,true)||backupPowerFault();
}
#endif
#if CSP_ENABLE_LOCAL_DASHBOARD
bool dashboard_stop_failed=false;
#endif
uint32_t backup_retiring_generation=0,backup_retry_at=0;
pro::WifiReconnect reconnect;
uint32_t reconnect_tick=0;
bool reconnect_failure_recorded=false;
void pauseBackup(){
#if CSP_ENABLE_LOCAL_DASHBOARD
  dashboard_stop_failed=!pro::local_dashboard::stop();
#endif
  set_maintenance(true);backup_pause_requested=true;
  backup_retiring_generation=backup_worker_request_retire();
}
bool backupReclaimed(){
  auto state=backup_worker_snapshot();
  if(state.phase==BackupWorkerPhase::Drained||state.phase==BackupWorkerPhase::Failed){
    if(state.phase==BackupWorkerPhase::Drained&&backup_retiring_generation&&
       state.retired_generation!=backup_retiring_generation)return false;
    if(!backup_worker_reap())return false;
    state=backup_worker_snapshot();
  }
  if(state.phase!=BackupWorkerPhase::Stopped)return false;
#if CSP_BACKUP_WIFI_AWAKE
  return releaseBackupPower();
#else
  return true;
#endif
}
// Owner-loop only. Never resume until the BLE/radio cleanup proof is complete.
void resumeBackup(uint32_t now){
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  if(publicBlocks())return;
#endif
  if(!backup_config_valid||backup_bridge_fault||store_fault||
     !radio_ready||closing||lifecycle.state()!=pro::BleLifecycle::State::Idle||
     owner_state!=WifiOwnerLoad::Ready||ble_management_active())return;
  if(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Fault)return;
  if(wifi_setup_radio_snapshot().state!=WifiSetupRadioState::LocalConnected)return;
  if(backup_pause_requested){
    if(!backupReclaimed())return;
    backup_pause_requested=false;backup_retiring_generation=0;
  }
  auto state=backup_worker_snapshot();
  if(state.phase==BackupWorkerPhase::Failed){
    if(!backupReclaimed())return;
    state=backup_worker_snapshot();
  }
  if(state.phase!=BackupWorkerPhase::Stopped)return;
#if CSP_BACKUP_WIFI_AWAKE
  if(!releaseBackupPower())return;
#endif
  if(backup_retry_at&&int32_t(now-backup_retry_at)<0)return;
  // Initial conservative admission; target TLS/stack margins still require
  // physical acceptance. Never lower BLE's separate 160 KiB admission gate.
  if(freeInternal()<64*1024||largestInternal()<32*1024)return;
  // No cloud credential/CA copy is retained by the owner loop during BLE.
  // The worker owns one immutable configuration until retirement/reap.
  auto config=load_backup_config();
  if(!config.is_valid){backup_config_valid=false;display="Cloud setup unavailable\nFiles stay on USB";return;}
#if CSP_ENABLE_OWNER_CLOUD_SETUP
  if(cloud_clock_pending){
    // Clock/network initialization waits until BLE has fully closed and its
    // memory proof passed. The SNTP hostname has app-lifetime storage.
    configTzTime(config.timezone.c_str(),time_server);cloud_clock_pending=false;
    if(freeInternal()<64*1024||largestInternal()<32*1024)return;
  }
#endif
#if CSP_BACKUP_FIXTURE_ONLY
  // Explicit test image: never enumerate existing QA files or publish into the
  // ordinary backup destination. Overrides exist only in this worker's RAM.
  config.watch_folder="CSP-INTEGRATION-TEST";
  config.server_url+="CSP-INTEGRATION-TEST-20261104/";
  config.backup_mode="idle";config.idle_seconds=30;config.sync_interval_s=30;
#endif
#if CSP_BACKUP_WIFI_AWAKE
  // The worker lives across its quiet/overnight idle waits, so Wi-Fi stays
  // awake through those waits too. This changes only the current driver mode;
  // previous policy is restored after full reap, before BLE/radio admission.
  if(!backup_power.acquire(backup_power_driver)){backupPowerFault();return;}
#endif
  if(!backup_worker_start(config)){
    backup_retry_at=now+5000;display="Backup start failed\nRetrying safely";
  }else backup_retry_at=0;
}
__attribute__((noinline)) void recoverWifi(uint32_t now){
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  if(publicBlocks())return;
#endif
  if(backup_bridge_fault||store_fault||!radio_ready||closing||ble_management_active()||
     owner_state!=WifiOwnerLoad::Ready||lifecycle.state()!=pro::BleLifecycle::State::Idle)return;
  const auto radio=wifi_setup_radio_snapshot().state;
  if(radio==WifiSetupRadioState::LocalConnected){
    reconnect.connected(wifi_setup_radio_snapshot().profile);reconnect_failure_recorded=false;return;
  }
  if(radio!=WifiSetupRadioState::Idle&&radio!=WifiSetupRadioState::Failed){
    reconnect_failure_recorded=false;return;
  }
  if(reconnect_tick&&uint32_t(now-reconnect_tick)<1000)return;
  reconnect_tick=now;
  if(!backup_pause_requested)pauseBackup();
  if(!backupReclaimed())return; // Never switch a radio underneath TLS/read work.
  StoredNetworks active;
  if(!model.exportActive(active)){eraseSecretMemory(&active,sizeof active);return;}
  reconnect.revision(active.revision);
  if(radio==WifiSetupRadioState::Failed&&!reconnect_failure_recorded){
    reconnect.failed(wifi_setup_radio_snapshot().profile,now);reconnect_failure_recorded=true;
  }
  const auto id=reconnect.choose(active,now);
  for(size_t i=0;id&&i<active.count;++i){
    const auto& profile=active.profiles[i];if(profile.id!=id)continue;
    reconnect_failure_recorded=false;
    if(!wifi_setup_radio_connect({profile.id,profile.ssid.data(),profile.secret.data(),profile.security})){
      reconnect.failed(id,now);reconnect_failure_recorded=true;
    }
    break;
  }
  eraseSecretMemory(&active,sizeof active);
}
#endif
uint32_t freeInternal(){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
uint32_t largestInternal(){return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
bool same(const WifiSetupSession& a,const WifiSetupSession& b){return a.id==b.id&&a.generation==b.generation;}
Access access(const WifiSetupSession& s){
  const bool admitted=owner_state==WifiOwnerLoad::Ready&&ble_management_authorizes(s);
  return {s.generation,admitted?Proof::DeviceOwner:Proof::None,admitted,!admitted,true};
}
#if CSP_ENABLE_OWNER_LOGIN || CSP_ENABLE_OWNER_CLOUD_SETUP
bool activeOwnerWindow(const WifiSetupSession& s,uint32_t now){
  const bool authorized=ble_management_authorizes(s);
  return authorized&&lifecycle.activeWindow(now,authorized);
}
#endif
struct Mailbox {
  enum class State:uint8_t {Empty,Queued,Running,Done};
  State state=State::Empty;
  uint32_t job=0,next_job=1;
  WifiSetupSession session;
  size_t input_size=0,output_size=0;
  std::array<uint8_t,WifiWireLimit> input{};
  std::array<char,WifiResultLimit> output{};
};
std::mutex mailbox_mutex;
Mailbox mailbox;
char serial_input[1024]{};size_t serial_size=0;bool serial_overflow=false;
// Sanitized read-only diagnostics. No secret, verifier or SSID is exposed.
enum OwnerBlock:uint32_t {OwnerUnavailable=1,OwnerBleActive=2,OwnerButtonUp=4,
  OwnerHoldShort=8,OwnerMediaDetected=16,OwnerRadioUnready=32,
  OwnerPlatformUnready=64,OwnerSettingsFault=128,OwnerRadioBusy=256,OwnerRadioFault=512};
uint32_t last_owner_blocked=0;
uint32_t ownerHeldMs(uint32_t now,bool down){return down&&button_down?uint32_t(now-button_down):0;}
uint32_t ownerBlocked(uint32_t now,bool down){
  uint32_t blocked=0;
  if(owner_state!=WifiOwnerLoad::Missing)blocked|=OwnerUnavailable;
  if(ble_management_active())blocked|=OwnerBleActive;
  if(!down)blocked|=OwnerButtonUp;
  else if(!button_down||ownerHeldMs(now,down)<10000)blocked|=OwnerHoldShort;
  if(storage_media_detected())blocked|=OwnerMediaDetected;
  if(!radio_ready)blocked|=OwnerRadioUnready;
  if(!ble_platform_ready())blocked|=OwnerPlatformUnready;
  if(store_fault)blocked|=OwnerSettingsFault;
  const auto state=wifi_setup_radio_snapshot().state;
  if(state==WifiSetupRadioState::Connecting||state==WifiSetupRadioState::Scanning||state==WifiSetupRadioState::Cancelling)blocked|=OwnerRadioBusy;
  if(state==WifiSetupRadioState::Fault)blocked|=OwnerRadioFault;
  return blocked;
}

void clearMailbox(){
  std::lock_guard<std::mutex> lock(mailbox_mutex);
  eraseSecretMemory(mailbox.input.data(),mailbox.input.size());
  eraseSecretMemory(mailbox.output.data(),mailbox.output.size());
  mailbox.input_size=mailbox.output_size=0;mailbox.state=Mailbox::State::Empty;
}
void reapMailbox(){
  WifiSetupSession stamp;uint32_t job=0;
  {std::lock_guard<std::mutex> lock(mailbox_mutex);stamp=mailbox.session;job=mailbox.job;}
  if(!stamp.generation||ble_management_authorizes(stamp))return;
  std::lock_guard<std::mutex> lock(mailbox_mutex);
  if(!same(mailbox.session,stamp)||mailbox.job!=job)return;
  eraseSecretMemory(mailbox.input.data(),mailbox.input.size());eraseSecretMemory(mailbox.output.data(),mailbox.output.size());
  mailbox.input_size=mailbox.output_size=0;mailbox.state=Mailbox::State::Empty;
}
void restoreActive(){
  check_pending=false;
  if(!radio_ready||!wifi_setup_radio_disconnect())return;
  if(store_fault)return;
  StoredNetworks active;
  if(model.exportActive(active))for(size_t i=0;i<active.count;++i){
    const auto& p=active.profiles[i];if(p.id==active.selected&&p.enabled){
      wifi_setup_radio_connect({p.id,p.ssid.data(),p.secret.data(),p.security});break;
    }
  }
  eraseSecretMemory(&active,sizeof active);
}
void endTransaction(bool restore=true){
#if CSP_ENABLE_OWNER_LOGIN
  clearLogin();
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP
  clearCloud();
#endif
  const bool had_candidate=transaction_session.generation||check_pending;
  if(transaction_session.generation)model.closeSession(transaction_session.generation);
  transaction_session={};check_session={};check_pending=false;if(had_candidate&&restore)restoreActive();
}
void error(JsonDocument& d,Error e){d.clear();d["ok"]=false;d["error"]=unsigned(e);}
#if CSP_ENABLE_OWNER_LOGIN
bool isLoginOp(Op op){return op==Op::LoginBegin||op==Op::LoginStage||op==Op::LoginCommit||op==Op::LoginAbort;}
bool loginAuthorized(void* context){
  const auto* draft=static_cast<LoginDraft*>(context);
  return login_draft.get()==draft&&!login_fault&&!login_barrier.load()&&maintenance_active()&&
    backup_worker_snapshot().phase==BackupWorkerPhase::Stopped&&activeOwnerWindow(draft->session,millis())&&
    ble_management_authorizes(draft->session)&&login_draft->candidate.status(access(draft->session),millis())==Error::None;
}
bool loginArm(void* context){
  auto* draft=static_cast<LoginDraft*>(context);
  std::lock_guard<std::mutex> publication(login_output_mutex);
  if(!loginAuthorized(context))return false;
  const auto now=millis();std::lock_guard<std::mutex> lock(mailbox_mutex);
  if(mailbox.state!=Mailbox::State::Running||!same(mailbox.session,draft->session)||
     !login_handoff.arm(draft->session.id,draft->session.generation,mailbox.job,now))return false;
  login_barrier.store(true);return true;
}
void loginReply(JsonDocument& out,uint64_t transaction,uint64_t revision,uint32_t edit,uint16_t received,bool durable=false){
  char device[13]{};for(size_t i=0;i<owner.mac.size();++i)std::snprintf(device+2*i,3,"%02x",owner.mac[i]);
  out["ok"]=true;out["error"]=0;out["transaction"]=std::to_string(transaction);out["revision"]=std::to_string(revision);
  out["edit"]=edit;out["received"]=received;out["device"]=device;out["durable"]=durable;
  out["reconnect_required"]=durable;out["store_fault"]=false;
}
void loginError(JsonDocument& out,Error state){error(out,state);out["store_fault"]=login_fault;}
void loginTick(uint32_t now){
  if(login_draft&&(!ble_management_authorizes(login_draft->session)||
       login_draft->candidate.status(access(login_draft->session),now)!=Error::None))clearLogin();
  const auto status=ble_management_status();
  std::lock_guard<std::mutex> lock(mailbox_mutex);
  if(login_handoff.shouldClose(now,status.connected))login_close_requested=true;
}
bool loginReload(){
  uint64_t revision=0;bool fault=false;
  {std::lock_guard<std::mutex> lock(mailbox_mutex);if(!login_handoff.active())return true;
    revision=login_handoff.revision();fault=login_handoff.fault()||!login_handoff.durable();}
  if(fault||ble_management_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped)return false;
  login::Store currentStore(login_nvs,owner.mac.data());login::Current current;OwnerRecord replacement;
  const bool valid=currentStore.load(current)==login::Loaded::Ready&&current.revision==revision&&
    decodeOwner(current.bytes.value.data(),current.bytes.value.size(),owner.mac.data(),replacement);
  const bool memory=pro::BleLifecycle::restored(before_free,before_largest,freeInternal(),largestInternal());
  if(valid&&memory)owner=replacement;eraseSecretMemory(&replacement,sizeof replacement);
  if(!valid||!memory)return false;
  {std::lock_guard<std::mutex> lock(mailbox_mutex);login_handoff.clear();login_barrier.store(false);}
  login_close_requested=false;return true;
}
__attribute__((noinline)) bool loginCommand(const WifiSetupSession& session,const WireRequest& request,JsonDocument& out){
  if(!isLoginOp(request.op))return false;
  const auto now=millis();const auto a=access(session);
  if(login_fault){loginError(out,Error::Unavailable);return true;}
  if(!Candidate::authorized(a)||!activeOwnerWindow(session,now)){loginError(out,Error::Denied);return true;}
  if(!maintenance_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped){loginError(out,Error::WorkersBusy);return true;}
  if(freeInternal()<login::OperationFreeFloor||largestInternal()<login::OperationLargestFloor){loginError(out,Error::Unavailable);return true;}
  if(request.op==Op::LoginBegin){
    if(login_draft||transaction_session.generation||check_pending
#if CSP_ENABLE_OWNER_CLOUD_SETUP
       ||cloud_draft
#endif
       ){loginError(out,Error::Busy);return true;}
    if(login_next_transaction==UINT64_MAX){loginError(out,Error::Exhausted);return true;}
    auto draft=std::unique_ptr<LoginDraft>(new(std::nothrow) LoginDraft);
    if(!draft){loginError(out,Error::Unavailable);return true;}
    login::Store currentStore(login_nvs,owner.mac.data());const auto loaded=currentStore.load(draft->base);
    if(loaded!=login::Loaded::Ready){loginError(out,loaded==login::Loaded::Corrupt?Error::Invalid:Error::Unavailable);return true;}
    OwnerRecord activeOwner;const bool sameOwner=decodeOwner(draft->base.bytes.value.data(),draft->base.bytes.value.size(),owner.mac.data(),activeOwner)&&
      activeOwner.salt==owner.salt&&activeOwner.verifier==owner.verifier;
    eraseSecretMemory(&activeOwner,sizeof activeOwner);
    if(!sameOwner){loginError(out,Error::StaleRevision);return true;}
    const auto state=draft->candidate.begin(a,++login_next_transaction,draft->base.revision,now);
    if(state!=Error::None){loginError(out,state);return true;}
    draft->session=session;login_draft=std::move(draft);
    loginReply(out,login_draft->candidate.transaction(),login_draft->candidate.revision(),0,0);return true;
  }
  if(!login_draft||!same(login_draft->session,session)){loginError(out,Error::StaleTransaction);return true;}
  if(request.op==Op::LoginAbort){
    const auto revision=login_draft->candidate.revision();const auto state=login_draft->candidate.abort(a,request.transaction);
    if(state!=Error::None){loginError(out,state);return true;}
    clearLogin();loginReply(out,request.transaction,revision,0,0);return true;
  }
  if(request.op==Op::LoginStage){
    const auto state=login_draft->candidate.stage(a,request.transaction,request.edit,request.cursor,request.login_bytes.data(),request.login_size,now);
    if(state!=Error::None){loginError(out,state);return true;}
    loginReply(out,request.transaction,login_draft->candidate.revision(),login_draft->candidate.edit(),login_draft->candidate.received());return true;
  }
  login::Bytes replacement;
  const auto state=login_draft->candidate.prepare(a,request.transaction,request.revision,request.edit,request.confirm,owner.mac.data(),now,replacement.value);
  if(state!=Error::None){loginError(out,state);return true;}
  login::Store currentStore(login_nvs,owner.mac.data());uint64_t revision=0;
  const auto saved=currentStore.publish(login_draft->base,replacement,request.revision,loginAuthorized,login_draft.get(),revision,loginArm,login_draft.get());
  if(saved==login::Saved::Unknown){
    {std::lock_guard<std::mutex> lock(mailbox_mutex);login_handoff.uncertain();}
    login_fault=true;backup_bridge_fault=true;set_maintenance(true);clearLogin();
    display="Owner login needs review\nFiles stay on USB";loginError(out,Error::Unavailable);return true;
  }
  if(saved!=login::Saved::Committed){
    loginError(out,saved==login::Saved::Conflict?Error::StaleRevision:saved==login::Saved::Cancelled?Error::Denied:
      saved==login::Saved::Unavailable?Error::Unavailable:Error::Invalid);return true;
  }
  {std::lock_guard<std::mutex> lock(mailbox_mutex);login_handoff.committed(revision);}
  clearLogin();display="Owner login saved\nReconnect with new login";
  loginReply(out,request.transaction,revision,request.edit,OwnerRecordSize,true);return true;
}
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP
void cloudPresence(JsonDocument& out,const cloud::Info& info){
  out["has_username"]=info.has_username;out["has_password"]=info.has_password;out["has_folder"]=info.has_folder;
}
void cloudUnknown(JsonDocument& out){
  clearCloud();cloud_store_fault=true;backup_config_valid=false;backup_bridge_fault=true;
  set_maintenance(true);pro::history::release();
  display="Cloud settings need review\nFiles stay on USB";
  error(out,Error::Unavailable);out["store_fault"]=true;
}
bool cloudAuthorized(void* pointer){
  const auto* draft=static_cast<CloudDraft*>(pointer);
  return cloud_draft.get()==draft&&!cloud_store_fault&&maintenance_active()&&
    backup_worker_snapshot().phase==BackupWorkerPhase::Stopped&&activeOwnerWindow(draft->session,millis())&&ble_management_authorizes(draft->session)&&
    cloud_draft->candidate.status(access(draft->session),millis())==Error::None;
}
void cloudTick(uint32_t now){
  if(cloud_draft&&(!ble_management_authorizes(cloud_draft->session)||
       cloud_draft->candidate.status(access(cloud_draft->session),now)!=Error::None))clearCloud();
}
bool isCloudOp(Op op){return op==Op::CloudStatus||op==Op::CloudBegin||op==Op::CloudStage||op==Op::CloudCommit||op==Op::CloudAbort;}
// The ordinary worker/mailbox invokes this on the existing owner loop, never
// on a GATT callback. Each operation is bounded; no network client is created.
__attribute__((noinline)) bool cloudCommand(const WifiSetupSession& session,const WireRequest& request,JsonDocument& out){
  if(!isCloudOp(request.op))return false;
  const auto a=access(session);const auto now=millis();
  if(!Candidate::authorized(a)||!activeOwnerWindow(session,now)){error(out,Error::Denied);return true;}
  if(!maintenance_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped){error(out,Error::WorkersBusy);return true;}
  if(request.op==Op::CloudStatus&&cloud_store_fault){
    cloud::Info unavailable;unavailable.revision=cloud_confirmed.revision;
    out["ok"]=true;out["revision"]=std::to_string(unavailable.revision);out["configured"]=false;out["provider"]="none";
    cloudPresence(out,unavailable);out["editing"]=false;out["edit"]=0;out["store_fault"]=true;return true;
  }
  if(cloud_store_fault){error(out,Error::Unavailable);out["store_fault"]=true;return true;}
  // Preserve the existing BLE active floors; optional parsing/serialization is
  // refused before it can consume the conservative extra working allowance.
  if(freeInternal()<cloud::OperationFreeFloor||largestInternal()<cloud::OperationLargestFloor){error(out,Error::Unavailable);return true;}
  if(request.op==Op::CloudStatus){
    auto current=std::unique_ptr<cloud::Record>(new(std::nothrow) cloud::Record);
    if(!current){error(out,Error::Unavailable);return true;}
    const auto loaded=cloud_store.load(*current);
    if(loaded==cloud::Loaded::Unavailable||loaded==cloud::Loaded::Corrupt){error(out,loaded==cloud::Loaded::Corrupt?Error::Invalid:Error::Unavailable);return true;}
    cloud_confirmed=current->info;auto info=current->info;
    if(cloud_draft){const auto state=cloud_draft->candidate.status(a,now);if(state!=Error::None)clearCloud();}
    const bool editing=bool(cloud_draft);
    if(editing){const auto presence=cloud_draft->candidate.presence();info.has_username=presence.has_username;info.has_password=presence.has_password;info.has_folder=presence.has_folder;}
    out["ok"]=true;out["revision"]=std::to_string(current->info.revision);out["configured"]=info.configured;
    out["provider"]=info.provider==cloud::Provider::Koofr?"koofr":info.provider==cloud::Provider::Other?"other":"none";
    cloudPresence(out,info);out["editing"]=editing;out["edit"]=editing?cloud_draft->candidate.edit():0;
    out["store_fault"]=false;return true;
  }
  if(request.op==Op::CloudBegin){
    if(cloud_draft||transaction_session.generation||check_pending){error(out,Error::Busy);return true;}
    if(cloud_next_transaction==UINT64_MAX){error(out,Error::Exhausted);return true;}
    auto next=std::unique_ptr<CloudDraft>(new(std::nothrow) CloudDraft);
    if(!next){error(out,Error::Unavailable);return true;}
    const auto loaded=cloud_store.load(next->base);
    if(loaded==cloud::Loaded::Unavailable||loaded==cloud::Loaded::Corrupt){error(out,loaded==cloud::Loaded::Corrupt?Error::Invalid:Error::Unavailable);return true;}
    const auto transaction=++cloud_next_transaction;
    const auto state=next->candidate.begin(a,request.revision,next->base.info.revision,transaction,now,next->base.initial);
    if(state!=Error::None){error(out,state);return true;}
    next->session=session;cloud_confirmed=next->base.info;cloud_draft=std::move(next);
    out["ok"]=true;out["revision"]=std::to_string(cloud_draft->candidate.revision());out["transaction"]=std::to_string(transaction);out["edit"]=0;
    cloudPresence(out,cloud_draft->candidate.presence());return true;
  }
  if(!cloud_draft||!same(cloud_draft->session,session)){error(out,Error::StaleTransaction);return true;}
  if(request.op==Op::CloudStage){
    const auto state=cloud_draft->candidate.stage(a,request.transaction,request.edit,now,request.cloud_field,request.cloud_value.data());
    if(state!=Error::None){error(out,state);return true;}
    out["ok"]=true;out["transaction"]=std::to_string(cloud_draft->candidate.transaction());out["edit"]=cloud_draft->candidate.edit();
    cloudPresence(out,cloud_draft->candidate.presence());return true;
  }
  if(request.op==Op::CloudAbort){
    const auto state=cloud_draft->candidate.abort(a,request.transaction);
    if(state!=Error::None){error(out,state);return true;}
    clearCloud();out["ok"]=true;return true;
  }
  cloud::Values values;
  const auto state=cloud_draft->candidate.prepare(a,request.transaction,request.revision,request.edit,now,request.accept_unverified,values);
  if(state!=Error::None){error(out,state);return true;}
  struct NetworkScratch {StoredNetworks value;~NetworkScratch(){eraseSecretMemory(&value,sizeof value);}};
  auto saved=std::unique_ptr<NetworkScratch>(new(std::nothrow) NetworkScratch);
  if(!saved){error(out,Error::Unavailable);return true;}
  if(!model.exportActive(saved->value)){error(out,Error::CheckRequired);return true;}
  const StoredProfile* selected=nullptr;
  for(size_t i=0;i<saved->value.count;++i)if(saved->value.profiles[i].id==saved->value.selected&&saved->value.profiles[i].enabled)selected=&saved->value.profiles[i];
  if(!selected){error(out,Error::CheckRequired);return true;}
  cloud::PublicSettings applied;
  const auto result=cloud_store.publish(cloud_draft->base,values,selected,request.revision,cloudAuthorized,cloud_draft.get(),applied);
  if(result==cloud::Saved::Unknown){cloudUnknown(out);return true;}
  if(result!=cloud::Saved::Committed){
    error(out,result==cloud::Saved::Conflict?Error::StaleRevision:result==cloud::Saved::Cancelled?Error::Denied:
      result==cloud::Saved::Exhausted?Error::Exhausted:result==cloud::Saved::Unavailable?Error::Unavailable:Error::Invalid);return true;
  }
  clearCloud(); // Free/scrub every staging/raw-secret allocation before resume.
  const size_t ntp=strnlen(applied.ntp_server.data(),applied.ntp_server.size());
  if(!ntp||ntp>=sizeof time_server||!backup_invalidate_cloud_status()){cloudUnknown(out);return true;}
  pro::history::release();std::memcpy(time_server,applied.ntp_server.data(),ntp);time_server[ntp]=0;
  cloud_clock_pending=true;backup_config_valid=true;cloud_confirmed.revision=applied.revision;
  cloud_confirmed.provider=cloud::Provider::Koofr;cloud_confirmed.configured=true;
  cloud_confirmed.has_username=cloud_confirmed.has_password=cloud_confirmed.has_folder=true;
  display="Cloud settings saved\nAccess not verified";
  out["ok"]=true;out["durable"]=true;out["revision"]=std::to_string(applied.revision);out["access_verified"]=false;return true;
}
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
__attribute__((noinline)) pro::public_wifi::Binding publicBinding(){
  StoredNetworks active;pro::public_wifi::Binding binding;
  if(!store_fault&&model.exportActive(active)){
    binding={active.selected,active.revision,false};
    for(size_t i=0;i<active.count;++i)if(active.profiles[i].id==active.selected&&active.profiles[i].enabled)binding.valid=true;
  }
  eraseSecretMemory(&active,sizeof active);return binding;
}
pro::public_wifi::Authority publicAuthority(const WifiSetupSession& session){
  const auto status=ble_management_status();const bool authorized=ble_management_authorizes(session);
  return {session.id,status.window_generation,session.generation,authorized,
    authorized&&lifecycle.state()==pro::BleLifecycle::State::Active,status.terminal_denial};
}
void publicResult(JsonDocument& result){
  const auto status=public_coordinator.snapshot(millis());result["ok"]=true;
  result["phase"]=pro::public_wifi::phaseName(status.phase);result["portal_error"]=unsigned(status.error);
  result["remaining_ms"]=status.remaining_ms;result["attempt_dav_read_verified"]=status.cloud_verified;
}
bool publicCommand(const WifiSetupSession& session,const WireRequest& r,JsonDocument& result){
  using PublicError=pro::public_wifi::Error;
  if(r.op==Op::PortalStatus){publicResult(result);return true;}
  if(r.op!=Op::PortalBegin&&r.op!=Op::PortalHandoff&&r.op!=Op::PortalCancel)return false;
  const auto authority=publicAuthority(session);auto binding=publicBinding();PublicError e=PublicError::None;
  if(r.op==Op::PortalCancel)e=public_coordinator.cancel(authority,r.grant,r.id,millis());
  else{
    if(r.profile!=binding.profile||r.revision!=binding.revision)binding.valid=false;
    if(r.op==Op::PortalBegin){
      StoredNetworks saved;const auto durable=store.load(saved);
      const bool selected=durable==Stored::Valid&&saved.revision==binding.revision&&saved.selected==binding.profile;
      eraseSecretMemory(&saved,sizeof saved);
      if(!selected||check_pending||transaction_session.generation||!backupReclaimed()||
         wifi_setup_radio_snapshot().state!=WifiSetupRadioState::LocalConnected||wifi_setup_radio_snapshot().profile!=binding.profile)
        e=PublicError::Busy;
      else{
        uint8_t mac[6]{};
        if(esp_read_mac(mac,ESP_MAC_BT)!=ESP_OK)e=PublicError::Denied;
        else{
          e=public_coordinator.begin(authority,binding,r.id,millis());
          if(e==PublicError::None){
            std::snprintf(public_ap_name,sizeof public_ap_name,"CSP-Guest-%02x%02x%02x",mac[3],mac[4],mac[5]);
            std::snprintf(public_signin_screen,sizeof public_signin_screen,"Join phone Wi-Fi\n%s\nPW: admin123",public_ap_name);
            public_session=session;public_handoff_job=0;public_result_returned=false;public_probe_issued=false;
          }
        }
      }
    }else e=public_coordinator.handoff(authority,binding,r.grant,r.id,millis());
  }
  if(e!=PublicError::None){error(result,e==PublicError::Denied?Error::Denied:Error::Busy);result["portal_error"]=unsigned(e);return true;}
  if(r.op==Op::PortalBegin){
    result.clear();result["ok"]=true;result["phase"]="prepared";
    result["grant"]=std::to_string(public_coordinator.snapshot(millis()).grant);
    result["ap_ssid"]=public_ap_name;result["ap_password"]="admin123";
    result["profile"]=binding.profile;result["revision"]=std::to_string(binding.revision);
    result["window_seconds"]=300;result["handoff_ms"]=30000;result["next"]="handoff_then_disconnect";
  }else{
    publicResult(result);
    if(r.op==Op::PortalHandoff)result["next"]="read_result_then_disconnect";
  }
  return true;
}
__attribute__((noinline)) void publicTick(uint32_t now){
  using namespace pro::public_wifi;
  using PublicPhase=pro::public_wifi::Phase;
  const auto phase=public_coordinator.snapshot(now).phase;
  if((phase==PublicPhase::Idle||phase==PublicPhase::Complete)&&probeSnapshot().phase==ProbePhase::Stopped&&
     pro::owned_guest::status().phase==pro::owned_guest::Phase::Stopped)return;
  pro::owned_guest::poll(now);
  const auto probe=probeSnapshot();
  if(probe.phase==ProbePhase::Drained||probe.phase==ProbePhase::Failed)probeReap();
  Observation observation;observation.binding=publicBinding();
  const auto ble=ble_management_status();observation.window=ble.window_generation;
  observation.ble_active=ble.active;observation.connected=ble.connected;
  observation.auth_matches=ble_management_authorizes(public_session);observation.terminal=ble.terminal_denial;
  observation.result_returned=public_result_returned.load();
  observation.worker_reclaimed=backupReclaimed();
  observation.ble_clean=!closing&&lifecycle.state()==pro::BleLifecycle::State::Idle&&recovery_checked&&recovery_restored&&close_stop_verified;
  observation.ble_failed=lifecycle.state()==pro::BleLifecycle::State::Blocked;
  pro::owned_guest::Uplink uplink;
  observation.sta_valid=wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected&&
    wifi_setup_radio_snapshot().profile==observation.binding.profile&&readUplink(uplink);
#if CSP_ENABLE_FILE_HISTORY
  // Portal routing and its independent TLS probe do not restart the backup
  // worker. Drop its paused-session cache once BLE is proven closed, before
  // either resource admission; a cleanup failure must still remain blocked.
  if(observation.ble_clean&&!observation.ble_active&&observation.worker_reclaimed&&
     pro::history::status().loaded)pro::history::release();
#endif
  observation.free=freeInternal();observation.largest=largestInternal();
  const auto route=pro::owned_guest::status();
  switch(route.phase){case pro::owned_guest::Phase::Starting:observation.route=Route::Starting;break;
    case pro::owned_guest::Phase::Active:observation.route=Route::Active;break;
    case pro::owned_guest::Phase::Stopping:observation.route=Route::Stopping;break;
    case pro::owned_guest::Phase::Blocked:observation.route=Route::Blocked;break;default:observation.route=Route::Stopped;}
  const bool expected_route_end=route.error==pro::owned_guest::Error::None||route.error==pro::owned_guest::Error::Expired;
  // Verified resource cleanup is distinct from a successful assistance window.
  // An AP-only failure can preserve STA; do not turn that into probe admission.
  if(route.phase==pro::owned_guest::Phase::Stopped&&!expected_route_end)observation.route=Route::Blocked;
  observation.route_clean=route.phase==pro::owned_guest::Phase::Stopped&&expected_route_end&&!route.resources_retained&&route.station_preserved;
  const auto finished=probeSnapshot();
  observation.probe_done=public_probe_issued&&finished.phase==ProbePhase::Stopped;
  observation.probe_reclaimed=finished.phase==ProbePhase::Stopped;observation.probe_ok=finished.ok;
  const auto action=public_coordinator.poll(observation,now);
  if(action==Action::CloseBle){pauseBackup();public_close_requested=true;}
  else if(action==Action::StartRoute){
    // Fixed routing-only AP credential remains independent of the owner key.
    pro::owned_guest::start({public_ap_name,"admin123",uplink,Coordinator::RouteMs});
  }else if(action==Action::StopRoute)pro::owned_guest::stop();
  else if(action==Action::StartProbe){public_probe_issued=true;probeStart();}
  else if(action==Action::CancelProbe)probeCancel();
  const auto status=public_coordinator.snapshot(now);
  if(status.phase==PublicPhase::Routing)display=pro::owned_guest::status().phase==pro::owned_guest::Phase::Active?
    public_signin_screen:"Starting sign-in Wi-Fi\nPlease wait";
  else if(status.phase==PublicPhase::StoppingRoute)display="Ending public sign-in\nPlease wait";
  else if(status.phase==PublicPhase::Probing||status.phase==PublicPhase::WaitingProbe)display="Checking cloud\nFiles stay on USB";
  else if(status.phase==PublicPhase::Recovering)display="Restoring backups\nPlease wait";
  else if(status.phase==PublicPhase::Blocked)display="Public Wi-Fi paused\nCheck status";
}
#endif
const char* radioName(WifiSetupRadioState s){
  switch(s){case WifiSetupRadioState::Scanning:return "scanning";case WifiSetupRadioState::Connecting:return "connecting";
    case WifiSetupRadioState::LocalConnected:return "local_connected";case WifiSetupRadioState::Failed:return "failed";
    case WifiSetupRadioState::Fault:return "fault";case WifiSetupRadioState::Cancelling:return "cancelling";default:return "idle";}
}
void activeStatus(JsonDocument& d,const WifiSetupSession& session){
  Snapshot s;const auto a=access(session);
  if(model.snapshot(a,millis(),false,s)!=Error::None){error(d,Error::Denied);return;}
  d["ok"]=true;d["revision"]=std::to_string(s.revision);
  d["durable"]=false; // replaced below only after confirmed store selection
  StoredNetworks saved;const auto loaded=store.load(saved);
  d["durable"]=loaded==Stored::Valid&&saved.revision==s.revision;
  if(loaded==Stored::Corrupt||loaded==Stored::IO||(loaded==Stored::Missing&&s.revision!=1))store_fault=true;
  eraseSecretMemory(&saved,sizeof saved);
  d["store_fault"]=store_fault;d["wifi"]=radioName(wifi_setup_radio_snapshot().state);
  StoredNetworks active;model.exportActive(active);
  d["selected_profile"]=active.selected;
  d["applied_revision"]=wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected&&
    wifi_setup_radio_snapshot().profile==active.selected?std::to_string(s.revision):std::string("0");
  eraseSecretMemory(&active,sizeof active);
  d["wifi_error"]=unsigned(wifi_setup_radio_snapshot().error);
  d["wifi_profile"]=wifi_setup_radio_snapshot().profile;
  d["internet_verified"]=false;d["cloud_available"]=false;d["backups_paused"]=true;
  if(transaction_session.generation&&same(session,transaction_session)){
    Snapshot pending;
    if(model.snapshot(a,millis(),true,pending)==Error::None){
      d["transaction"]=std::to_string(pending.transaction);d["edit"]=pending.edit;
      d["phase"]=unsigned(pending.phase);d["check_error"]=unsigned(pending.check_error);
    }
  }
}
// Keep command and serial scratch frames out of the continuously polled USB
// loop's frame. Target .su evidence is retained; dynamic stack still needs the
// separately approved device test.
__attribute__((noinline)) void perform(const WifiSetupSession& session,const WireRequest& r,JsonDocument& result){
  const uint32_t now=millis();const auto a=access(session);
  if(!Candidate::authorized(a)){error(result,Error::Denied);return;}
#if CSP_ENABLE_OWNER_LOGIN
  if(login_barrier.load()){loginError(result,Error::Denied);return;}
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  if(isLoginOp(r.op)&&public_coordinator.prepared()){loginError(result,Error::Busy);return;}
#endif
  if(loginCommand(session,r,result))return;
  if(login_draft&&r.op!=Op::Status&&r.op!=Op::BackupStatus){error(result,Error::Busy);return;}
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP && CSP_ENABLE_FILE_HISTORY
  if((cloud_draft||cloud_store_fault)&&(r.op==Op::HistoryStatus||r.op==Op::HistoryRow)){error(result,Error::Busy);return;}
#endif
#if CSP_ENABLE_FILE_HISTORY
  if(r.op==Op::HistoryStatus||r.op==Op::HistoryRow){
    if(!maintenance_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped){error(result,Error::Busy);return;}
    if(r.op==Op::HistoryStatus){
      const auto s=pro::history::status();result["ok"]=true;result["g"]=s.revision;result["n"]=s.count;result["matched"]=s.matched;
      result["when"]=std::to_string(s.captured_unix);result["loaded"]=s.loaded;result["complete"]=s.complete;result["limited"]=s.limited;
      result["corrupt"]=s.corrupt;result["io_failed"]=s.io_failed;result["allocation_failed"]=s.allocation_failed;result["exhausted"]=s.exhausted;return;
    }
    pro::history::Fragment f;const auto state=pro::history::read(r.edit,r.profile,r.cursor,f);
    if(state!=pro::history::Read::Found){error(result,state==pro::history::Read::Stale?Error::StaleRevision:state==pro::history::Read::Unavailable?Error::Unavailable:Error::Invalid);return;}
    result["ok"]=true;result["g"]=f.revision;result["i"]=f.index;result["n"]=f.count;result["b"]=std::to_string(f.bytes);
    result["s"]=f.state;result["v"]=std::to_string(f.verified);result["r"]=std::to_string(f.retry);result["e"]=f.error;result["a"]=f.attempts;
    result["p"]=f.path_bytes;result["o"]=f.offset;result["x"]=f.next;result["m"]=f.more;result["h"]=f.hex;return;
  }
#endif
#if CSP_ENABLE_EVENT_LOG && CSP_ENABLE_BACKUP_SETUP
  if(r.op==Op::EventLog){
    // RAM-only access. The normal authenticated window has already retired the
    // metadata owner; never read LittleFS from a BLE callback or command job.
    if(!maintenance_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped){error(result,Error::Busy);return;}
    const auto page=pro::events::readPage(r.cursor);const auto& state=page.status;
    if(r.cursor>state.newest_sequence){error(result,Error::Invalid);return;}
    result["ok"]=true;auto rows=result["events"].to<JsonArray>();
    // Exactly one event keeps even maximum-width numeric values inside the
    // existing 400-byte result frame. Omit byte counts and all free-form text.
    if(page.count){const auto& event=page.events[0];auto row=rows.add<JsonObject>();
      row["s"]=event.sequence;row["u"]=event.uptime_ms;row["t"]=event.utc_unix;
      row["k"]=unsigned(event.kind);row["c"]=event.code;}
    result["next"]=page.count?page.events[0].sequence:r.cursor;
    result["more"]=page.count>1||page.more;
    result["newest"]=state.newest_sequence;result["persisted"]=state.persisted_sequence;
    result["loaded"]=state.loaded;result["dirty"]=state.dirty;
    result["corrupt"]=state.corrupt;result["io_failed"]=state.io_failed;
    result["exhausted"]=state.sequence_exhausted;return;
  }
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
#if CSP_ENABLE_OWNER_CLOUD_SETUP
  if(cloud_draft&&(r.op==Op::PortalBegin||r.op==Op::PortalHandoff)){
    error(result,Error::Busy);return;
  }
#endif
  if(publicCommand(session,r,result))return;
  if(public_coordinator.prepared()&&r.op!=Op::Status&&r.op!=Op::BackupStatus){error(result,Error::Busy);return;}
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP
  if(cloudCommand(session,r,result))return;
  if(cloud_draft&&(r.op==Op::Begin||r.op==Op::Stage||r.op==Op::Forget||r.op==Op::Check||r.op==Op::Commit||r.op==Op::Abort||r.op==Op::Scan)){
    error(result,Error::Busy);return;
  }
#endif
#if CSP_ENABLE_BACKUP_SETUP
  if(r.op==Op::BackupStatus){
    const auto backup=backup_status();result["ok"]=true;
    result["backups_paused"]=maintenance_active();result["cloud_configured"]=backup_config_valid;
    result["last_backup_unix"]=backup.lastBackup;result["verified"]=backup.verified;
    result["pending"]=backup.pending;result["failed"]=backup.failed;
    result["backup_error"]=backup.error;return;
  }
#endif
  if(r.op==Op::Status){activeStatus(result,session);return;}
  if(r.op==Op::List){
    Snapshot s;if(auto e=model.snapshot(a,now,false,s);e!=Error::None){error(result,e);return;}
    result["ok"]=true;result["revision"]=std::to_string(s.revision);result["count"]=s.count;
    auto profiles=result["profiles"].to<JsonArray>();size_t next=r.cursor;
    for(size_t i=r.cursor;i<s.count&&i<size_t(r.cursor)+2;++i){auto row=profiles.add<JsonObject>();const auto& p=s.profiles[i];
      row["id"]=p.id;row["ssid"]=p.ssid.data();row["open"]=p.security==Security::Open;
      row["has_secret"]=p.has_secret;row["priority"]=p.priority;row["enabled"]=p.enabled;next=i+1;}
    result["next"]=next<s.count?unsigned(next):0;
    return;
  }
  if(r.op==Op::Scan){
    if(check_pending){error(result,Error::Busy);return;}
    result["ok"]=wifi_setup_radio_scan();result["wifi"]=radioName(wifi_setup_radio_snapshot().state);return;
  }
  if(r.op==Op::ScanPage){
    const auto& scan=wifi_setup_radio_snapshot();
    result["ok"]=true;result["wifi"]=radioName(scan.state);result["has_scan"]=scan.has_scan;
    result["truncated"]=scan.truncated;result["error"]=unsigned(scan.error);
    auto rows=result["networks"].to<JsonArray>();size_t next=r.cursor;
    for(size_t i=r.cursor;i<scan.count&&i<size_t(r.cursor)+2;++i){auto row=rows.add<JsonObject>();const auto& p=scan.rows[i];
      row["ssid"]=p.ssid;row["rssi"]=p.rssi;row["open"]=p.open;row["supported"]=p.supported;next=i+1;}
    result["next"]=next<scan.count?unsigned(next):0;return;
  }
  if(store_fault){error(result,Error::Invalid);return;}
  if(wifi_setup_radio_snapshot().state==WifiSetupRadioState::Fault){error(result,Error::Unavailable);return;}
  if(r.op==Op::Begin){
    uint64_t tx=0;auto e=model.begin(a,r.revision,now,tx);if(e!=Error::None){error(result,e);return;}
    transaction_session=session;result["ok"]=true;result["transaction"]=std::to_string(tx);result["edit"]=0;return;
  }
  if(!same(session,transaction_session)){error(result,Error::StaleTransaction);return;}
  if(r.op==Op::Stage){
    if(check_pending){error(result,Error::Busy);return;}
    const Edit edit{r.profile,r.ssid.data(),r.open?Security::Open:Security::Wpa2Personal,
      r.keep?SecretAction::Keep:r.open?SecretAction::Forget:SecretAction::Replace,
      r.secret.data(),r.priority,r.enabled};uint32_t id=0;
    auto e=model.stage(a,r.transaction,now,edit,id);if(e!=Error::None){error(result,e);return;}
    Snapshot s;model.snapshot(a,now,true,s);result["ok"]=true;result["profile"]=id;result["edit"]=s.edit;return;
  }
  if(r.op==Op::Forget){
    if(check_pending){error(result,Error::Busy);return;}
    auto e=model.forget(a,r.transaction,now,r.profile,r.confirm);if(e!=Error::None){error(result,e);return;}
    result["ok"]=true;return;
  }
  if(r.op==Op::Check){
    if(check_pending||wifi_setup_radio_snapshot().state==WifiSetupRadioState::Scanning){error(result,Error::Busy);return;}
    auto e=model.startCheck(a,r.transaction,now,r.profile,checking);if(e!=Error::None){error(result,e);return;}
    bool started=false;
    e=model.borrowCheckProfile(a,checking,now,[](void* p,const CheckProfile& profile){*static_cast<bool*>(p)=wifi_setup_radio_connect(profile);},&started);
    if(e!=Error::None||!started){model.finishCheck(a,checking,millis(),Error::Unavailable);restoreActive();error(result,Error::Unavailable);return;}
    check_pending=true;check_session=session;result["ok"]=true;result["wifi"]="connecting";return;
  }
  if(r.op==Op::Abort){
    auto e=model.abort(a,r.transaction,now);if(e!=Error::None){error(result,e);return;}
    transaction_session={};restoreActive();result["ok"]=true;return;
  }
  if(r.op==Op::Commit){
    if(check_pending){error(result,Error::Busy);return;}
    StoredNetworks next;
    auto e=model.preparePersistent(a,r.transaction,r.edit,r.revision,now,r.accept_local,next);
    if(e!=Error::None){eraseSecretMemory(&next,sizeof next);error(result,e);return;}
    if(wifi_setup_radio_snapshot().state!=WifiSetupRadioState::LocalConnected||
       wifi_setup_radio_snapshot().profile!=next.selected){eraseSecretMemory(&next,sizeof next);error(result,Error::CheckRequired);return;}
    save_before_free=freeInternal();save_before_largest=largestInternal();
    const auto saved=store.publish(next,r.revision,[](void* p){return ble_management_authorizes(*static_cast<WifiSetupSession*>(p));},const_cast<WifiSetupSession*>(&session));
    save_after_free=freeInternal();save_after_largest=largestInternal();
    if(saved==Published::Committed){
      model.loadTrusted(next);transaction_session={};result["ok"]=true;result["durable"]=true;
      result["revision"]=std::to_string(next.revision);result["internet_verified"]=false;
      display="Wi-Fi saved\nInternet unverified";
      if(wifi_setup_radio_snapshot().state!=WifiSetupRadioState::LocalConnected||
         wifi_setup_radio_snapshot().profile!=next.selected)restoreActive();
    }else{
      if(saved==Published::Unknown){store_fault=true;model.closeSession(session.generation);transaction_session={};}
      error(result,saved==Published::Cancelled?Error::Denied:saved==Published::Conflict?Error::StaleRevision:Error::Invalid);
      restoreActive();
    }
    eraseSecretMemory(&next,sizeof next);return;
  }
  error(result,Error::Invalid);
}
__attribute__((noinline)) void worker(){
  std::array<uint8_t,WifiWireLimit> input{};size_t size=0;uint32_t job=0;WifiSetupSession session;
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex);if(mailbox.state!=Mailbox::State::Queued)return;
    input=mailbox.input;size=mailbox.input_size;job=mailbox.job;session=mailbox.session;
    eraseSecretMemory(mailbox.input.data(),mailbox.input.size());mailbox.input_size=0;mailbox.state=Mailbox::State::Running;
  }
  ScrubJsonAllocator allocator;JsonDocument result(&allocator);WireRequest request;
  bool admitted=ble_management_authorizes(session)&&parseWire(input.data(),size,request);
#if CSP_ENABLE_OWNER_LOGIN
  admitted=admitted&&!login_barrier.load();
#endif
  if(!admitted)error(result,Error::Denied);
  else perform(session,request,result);
  eraseSecretMemory(input.data(),input.size());
  std::array<char,WifiResultLimit> output{};size_t length=measureJson(result);
  if(result.overflowed()||length>=output.size()){
    error(result,Error::Invalid);length=measureJson(result);
  }
  if(!serializeJson(result,output.data(),output.size())){std::strcpy(output.data(),"{\"ok\":false,\"error\":7}");length=std::strlen(output.data());}
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex);
    if(mailbox.state==Mailbox::State::Running&&mailbox.job==job&&same(mailbox.session,session)){
#if CSP_ENABLE_OWNER_LOGIN
      if(login_barrier.load()&&(!admitted||request.op!=Op::LoginCommit||!login_handoff.matches(session.id,session.generation,job))){
        eraseSecretMemory(mailbox.output.data(),mailbox.output.size());mailbox.output_size=0;mailbox.state=Mailbox::State::Empty;return;
      }
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
      if(request.op==Op::PortalHandoff&&result["ok"]==true){public_handoff_job.store(job);public_result_returned.store(false);}
#endif
      mailbox.output=output;mailbox.output_size=length;mailbox.state=Mailbox::State::Done;
    }
  }
}
#if CSP_USB_BENCH_AUTOMATION && CSP_ENABLE_BACKUP_SETUP
// USB-only bench control; no stored Wi-Fi policy or driver lifecycle changes.
void powerSaveCommand(const char* input){
  const bool status=!std::strcmp(input,"POWER_SAVE_STATUS");
  const int requested=!std::strcmp(input,"POWER_SAVE_SET 0")?0:
                      !std::strcmp(input,"POWER_SAVE_SET 1")?1:-1;
  bool accepted=false;int mode=-1;esp_err_t sdk_error=ESP_ERR_INVALID_ARG;
  if(status||requested!=-1){
    sdk_error=ESP_ERR_INVALID_STATE;
    const bool eligible=owner_state==WifiOwnerLoad::Ready&&radio_ready&&!store_fault&&
      !backup_bridge_fault&&!closing&&!ble_management_active()&&
      lifecycle.state()==pro::BleLifecycle::State::Idle&&
      wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected&&
      backup_worker_snapshot().phase==BackupWorkerPhase::Running;
    bool policy_available=true;
#if CSP_BACKUP_WIFI_AWAKE
    policy_available=status||(!backup_power.owned()&&!backup_power.faulted());
#endif
    if(eligible&&policy_available){
      const auto desired=requested==0?WIFI_PS_NONE:WIFI_PS_MIN_MODEM;
      sdk_error=status?ESP_OK:esp_wifi_set_ps(desired);
      if(sdk_error==ESP_OK){
        wifi_ps_type_t observed=WIFI_PS_MIN_MODEM;
        sdk_error=esp_wifi_get_ps(&observed);
        if(sdk_error==ESP_OK){
          mode=int(observed);
          if(!status&&observed!=desired)sdk_error=ESP_ERR_INVALID_STATE;
          else accepted=true;
        }
      }
    }
  }
  USBSerial.printf("{\"accepted\":%s,\"mode\":%d,\"sdk_error\":%ld}\n",
    accepted?"true":"false",mode,static_cast<long>(sdk_error));
}
#endif
#if CSP_UPLOAD_DIAGNOSTICS
void uploadStatus(){
  const auto d=upload_diagnostics_snapshot();
  USBSerial.printf("{\"expected_bytes\":%llu,\"source_fread_bytes\":%llu,\"source_returned_bytes\":%llu,\"tls_requested_bytes\":%llu,\"tls_accepted_bytes\":%llu,\"net_requested_bytes\":%llu,\"net_accepted_bytes\":%llu,\"attempt\":%lu,\"request_ms\":%lu,\"socket_timeout_ms\":%lu,\"source_calls\":%lu,\"short_reads\":%lu,\"max_read_ms\":%lu,\"tls_write_calls\":%lu,\"tls_read_calls\":%lu,\"net_send_calls\":%lu,\"want_read\":%lu,\"want_write\":%lu,\"max_tls_write_ms\":%lu,\"last_positive_age_ms\":%lu,\"http_code\":%ld,\"source_errno\":%ld,\"tls_last_write_ret\":%ld,\"tls_first_fatal_write\":%ld,\"tls_last_read_ret\":%ld,\"tls_first_fatal_read\":%ld,\"net_last_ret\":%ld,\"net_first_fatal\":%ld,\"net_errno\":%ld,\"tls_write_errno\":%ld,\"tls_read_errno\":%ld,\"target\":%u,\"source_abort\":%u,\"enabled\":%s,\"active\":%s,\"completed\":%s,\"source_ferror\":%s,\"source_eof\":%s}\n",
    static_cast<unsigned long long>(d.expected_bytes),
    static_cast<unsigned long long>(d.source_fread_bytes),
    static_cast<unsigned long long>(d.source_returned_bytes),
    static_cast<unsigned long long>(d.tls_requested_bytes),
    static_cast<unsigned long long>(d.tls_accepted_bytes),
    static_cast<unsigned long long>(d.net_requested_bytes),
    static_cast<unsigned long long>(d.net_accepted_bytes),
    static_cast<unsigned long>(d.attempt),
    static_cast<unsigned long>(d.request_ms),
    static_cast<unsigned long>(d.socket_timeout_ms),
    static_cast<unsigned long>(d.source_calls),
    static_cast<unsigned long>(d.short_reads),
    static_cast<unsigned long>(d.max_read_ms),
    static_cast<unsigned long>(d.tls_write_calls),
    static_cast<unsigned long>(d.tls_read_calls),
    static_cast<unsigned long>(d.net_send_calls),
    static_cast<unsigned long>(d.want_read),
    static_cast<unsigned long>(d.want_write),
    static_cast<unsigned long>(d.max_tls_write_ms),
    static_cast<unsigned long>(d.last_positive_age_ms),
    static_cast<long>(d.http_code),
    static_cast<long>(d.source_errno),
    static_cast<long>(d.tls_last_write_ret),
    static_cast<long>(d.tls_first_fatal_write),
    static_cast<long>(d.tls_last_read_ret),
    static_cast<long>(d.tls_first_fatal_read),
    static_cast<long>(d.net_last_ret),
    static_cast<long>(d.net_first_fatal),
    static_cast<long>(d.net_errno),
    static_cast<long>(d.tls_write_errno),
    static_cast<long>(d.tls_read_errno),
    unsigned(d.target),
    unsigned(d.source_abort),
    d.enabled?"true":"false",
    d.active?"true":"false",
    d.completed?"true":"false",
    d.source_ferror?"true":"false",
    d.source_eof?"true":"false");
}
#endif
#if CSP_PANIC_DIAGNOSTICS
__attribute__((noinline)) void panicStatus(){
  char report[PanicDiagnosticJsonLimit];
  if(panic_diagnostics_json(report,sizeof report))USBSerial.println(report);
  else USBSerial.println("{\"panic_valid\":false}");
}
#endif
__attribute__((noinline)) void serial(){
  for(unsigned i=0;i<64&&USBSerial.available();++i){
    const char c=char(USBSerial.read());if(c=='\r')continue;
    if(c=='\n'){
      if(!serial_overflow&&(!last_serial_reply||uint32_t(millis()-last_serial_reply)>=250)){
        last_serial_reply=millis();
        serial_input[serial_size]=0;
        if(!std::strcmp(serial_input,"STATUS")){
          StoredNetworks active;model.exportActive(active);
          USBSerial.printf("{\"firmware\":\"%s\",\"setup_only\":%s,\"owner_ready\":%s,\"store_fault\":%s,\"ble\":%s,\"wifi\":\"%s\",\"saved_revision\":\"%llu\",\"internet_verified\":false,\"backups_paused\":%s,\"internal_free\":%lu,\"largest_block\":%lu}\n",
            FirmwareVersion,CSP_ENABLE_BACKUP_SETUP?"false":"true",owner_state==WifiOwnerLoad::Ready?"true":"false",store_fault?"true":"false",ble_management_active()?"true":"false",
            radioName(wifi_setup_radio_snapshot().state),static_cast<unsigned long long>(active.revision),
            (CSP_ENABLE_BACKUP_SETUP?maintenance_active():true)?"true":"false",static_cast<unsigned long>(freeInternal()),static_cast<unsigned long>(largestInternal()));
          eraseSecretMemory(&active,sizeof active);
          USBSerial.printf("{\"wifi_step\":%u,\"wifi_sdk_error\":%ld,\"wifi_error\":%u}\n",unsigned(wifi_setup_radio_snapshot().step),static_cast<long>(wifi_setup_radio_snapshot().sdk_error),unsigned(wifi_setup_radio_snapshot().error));
          USBSerial.printf("{\"ble_before_free\":%lu,\"ble_before_largest\":%lu,\"ble_after_free\":%lu,\"ble_after_largest\":%lu,\"ble_recovery_checked\":%s,\"ble_memory_restored\":%s,\"save_before_free\":%lu,\"save_before_largest\":%lu,\"save_after_free\":%lu,\"save_after_largest\":%lu,\"internal_minimum\":%lu,\"loop_stack_free\":%u,\"host_exit_stack_min\":%lu}\n",
            static_cast<unsigned long>(before_free),static_cast<unsigned long>(before_largest),static_cast<unsigned long>(after_free),static_cast<unsigned long>(after_largest),
            recovery_checked?"true":"false",recovery_restored?"true":"false",
            static_cast<unsigned long>(save_before_free),static_cast<unsigned long>(save_before_largest),static_cast<unsigned long>(save_after_free),static_cast<unsigned long>(save_after_largest),
            static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(uxTaskGetStackHighWaterMark(nullptr)),
            static_cast<unsigned long>(csp_ble_sdk_host_stack_min_bytes()));
        }
#if CSP_USB_BENCH_AUTOMATION
#if CSP_NVS_CACHE_LAB
        else if(!std::strcmp(serial_input,"BENCH_NVS_CACHE")||!std::strcmp(serial_input,"BENCH_NVS_CACHE_CLEAR")){
          const auto r=wifi_setup_radio_snapshot().state;
          const bool eligible=owner_state==WifiOwnerLoad::Ready&&!store_fault&&!storage_ready()&&
            ble_management_active()&&!ble_management_status().connected&&backupReclaimed()&&!closing&&
            lifecycle.state()==pro::BleLifecycle::State::Active&&
            r!=WifiSetupRadioState::Connecting&&r!=WifiSetupRadioState::Scanning&&r!=WifiSetupRadioState::Cancelling&&r!=WifiSetupRadioState::Fault;
          if(!std::strcmp(serial_input,"BENCH_NVS_CACHE_CLEAR")){
            const auto error=eligible?wifi_setup_cache_lab_clear():ESP_ERR_INVALID_STATE;
            USBSerial.printf("{\"accepted\":%s,\"sdk_error\":%ld}\n",error==ESP_OK?"true":"false",static_cast<long>(error));
          }else{
          const auto t=eligible?wifi_setup_cache_lab():WifiCacheLabResult{};
          USBSerial.printf("{\"accepted\":%s,\"sdk_error\":%ld,\"writes\":%lu,\"before_largest\":%lu,\"after_largest\":%lu}\n",
            t.accepted?"true":"false",static_cast<long>(t.sdk_error),static_cast<unsigned long>(t.writes),
            static_cast<unsigned long>(t.before_largest),static_cast<unsigned long>(t.after_largest));
          }
        }
        else if(!std::strcmp(serial_input,"BENCH_WIFI_DROP")){
          const auto b=backup_status();
          const bool eligible=owner_state==WifiOwnerLoad::Ready&&!store_fault&&!closing&&!ble_management_active()&&
            lifecycle.state()==pro::BleLifecycle::State::Idle&&wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected&&
            !b.active&&b.hasScan&&b.scanComplete;
          const bool accepted=eligible&&esp_wifi_disconnect()==ESP_OK;
          USBSerial.println(accepted?"{\"accepted\":true}":"{\"accepted\":false}");
        }
#endif
        else if(!std::strcmp(serial_input,"WIFI_LINK_STATUS")){
          StoredNetworks s;model.exportActive(s);
          USBSerial.printf("{\"wifi\":\"%s\",\"profile\":%lu,\"selected\":%lu,\"count\":%u}\n",
            radioName(wifi_setup_radio_snapshot().state),static_cast<unsigned long>(wifi_setup_radio_snapshot().profile),
            static_cast<unsigned long>(s.selected),unsigned(s.count));
          eraseSecretMemory(&s,sizeof s);
        }
        else if(!std::strcmp(serial_input,"BENCH_WINDOW")){
          // Owner-authorized local bench opening only. This is not ownership:
          // settings still require the unchanged private Security2 handshake.
          const bool eligible=owner_state==WifiOwnerLoad::Ready&&(CSP_ENABLE_BACKUP_SETUP||!storage_media_detected())&&
             radio_ready&&ble_platform_ready()&&!store_fault&&!closing&&!ble_management_active()
#if CSP_ENABLE_OWNER_LOGIN
             &&!login_barrier.load()&&!login_fault
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
             &&!publicBlocks()
#endif
             ;
          const bool opened=eligible&&lifecycle.requestWindow(millis())==pro::BleLifecycle::Action::Pause;
#if CSP_ENABLE_BACKUP_SETUP
          if(opened)pauseBackup();
#endif
          USBSerial.println(opened?"{\"bench_window\":true}":"{\"bench_window\":false}");
        }else if(!std::strcmp(serial_input,"BENCH_CLOSE")){
          const bool eligible=owner_state==WifiOwnerLoad::Ready&&!closing&&
            (lifecycle.state()==pro::BleLifecycle::State::Active||lifecycle.state()==pro::BleLifecycle::State::Waiting);
          bench_close_queued=eligible;
          USBSerial.println(eligible?"{\"bench_close\":true}":"{\"bench_close\":false}");
        }
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
        else if(!std::strcmp(serial_input,"PUBLIC_WIFI_STATUS")){
          const auto current=public_coordinator.snapshot(millis());const auto route=pro::owned_guest::status();
          USBSerial.printf("{\"public_wifi_phase\":\"%s\",\"portal_error\":%u,\"remaining_ms\":%lu,\"attempt_dav_read_verified\":%s,\"route_phase\":%u,\"resources_retained\":%s,\"probe_phase\":%u}\n",
            pro::public_wifi::phaseName(current.phase),unsigned(current.error),static_cast<unsigned long>(current.remaining_ms),
            current.cloud_verified?"true":"false",unsigned(route.phase),route.resources_retained?"true":"false",unsigned(pro::public_wifi::probeSnapshot().phase));
        }
#if CSP_PRODUCT_LAB6_PROFILE
        else if(!std::strcmp(serial_input,"PUBLIC_ROUTE_STATUS")){
          const auto route=pro::owned_guest::status();const auto diag=pro::owned_guest::diagnostic();
          USBSerial.printf("{\"route_phase\":%u,\"route_error\":%u,\"native_error\":%ld,\"generation\":%lu,\"station_preserved\":%s,\"resources_retained\":%s,\"failure_phase\":%u,\"failure_step\":%u,\"ap_proof\":%u}\n",
            unsigned(route.phase),unsigned(route.error),static_cast<long>(route.native_error),static_cast<unsigned long>(route.generation),
            route.station_preserved?"true":"false",route.resources_retained?"true":"false",unsigned(diag.failure_phase),unsigned(diag.failure_step),unsigned(diag.ap_proof));
        }
#endif
#endif
#endif
#if CSP_ENABLE_BACKUP_SETUP
#if CSP_BACKUP_WIFI_AWAKE
        else if(!std::strcmp(serial_input,"POWER_POLICY_STATUS")){
          // Sanitized RAM facts only, including while a fault blocks SDK access.
          USBSerial.printf("{\"owned\":%s,\"fault\":%u,\"sdk_error\":%d,\"previous\":%d}\n",
            backup_power.owned()?"true":"false",unsigned(backup_power.fault()),backup_power.error(),backup_power.previous());
        }
#endif
#if CSP_ENABLE_LOCAL_DASHBOARD
        else if(!std::strcmp(serial_input,"DASHBOARD_STATUS")){
          USBSerial.printf("{\"dashboard_listening\":%s,\"dashboard_fault\":%s,\"station_ip\":\"%s\"}\n",
            pro::local_dashboard::listening()?"true":"false",pro::local_dashboard::faulted()?"true":"false",WiFi.localIP().toString().c_str());
        }
#endif
#if CSP_USB_BENCH_AUTOMATION
        else if(!std::strncmp(serial_input,"POWER_SAVE_",11))powerSaveCommand(serial_input);
#if CSP_UPLOAD_DIAGNOSTICS
        else if(!std::strcmp(serial_input,"UPLOAD_STATUS"))uploadStatus();
#endif
#endif
        else if(!std::strcmp(serial_input,"MEMORY_STATUS")){
          const auto m=backup_memory_snapshot();
          USBSerial.printf("{\"failure_hook_registered\":%s,\"samples\":%lu,\"observed_min_free\":%lu,\"observed_min_largest\":%lu,\"allocation_failure_seen\":%s}\n",
            m.failure_hook_registered?"true":"false",(unsigned long)m.samples,
            (unsigned long)m.observed_min_free,(unsigned long)m.observed_min_largest,m.allocation_failure_seen?"true":"false");
        }else if(!std::strcmp(serial_input,"BACKUP_STATUS")){
          const auto b=backup_status();const auto w=backup_worker_snapshot();
          USBSerial.printf("{\"worker_phase\":%u,\"worker_generation\":%lu,\"worker_error\":%d,\"cloud_configured\":%s,\"paused\":%s,\"active\":%s,\"has_scan\":%s,\"scan_complete\":%s,\"verified\":%lu,\"pending\":%lu,\"failed\":%lu,\"last_backup_unix\":%llu,\"error\":%d,\"phase\":%u,\"done\":%llu,\"total\":%llu,\"speed\":%lu,\"worker_stack_free\":%lu,\"tls_minimum_heap\":%lu}\n",
            unsigned(w.phase),(unsigned long)w.generation,w.error,backup_config_valid?"true":"false",
            maintenance_active()?"true":"false",b.active?"true":"false",b.hasScan?"true":"false",b.scanComplete?"true":"false",
            (unsigned long)b.verified,(unsigned long)b.pending,(unsigned long)b.failed,(unsigned long long)b.lastBackup,b.error,
            unsigned(b.phase),(unsigned long long)b.transferDone,(unsigned long long)b.transferTotal,(unsigned long)b.transferSpeed,
            (unsigned long)b.stackFree,(unsigned long)b.minimumHeap);
        }else if(!std::strcmp(serial_input,"CHECK")){
          const bool accepted=request_connection_check(false);
          USBSerial.println(accepted?"{\"accepted\":true}":"{\"accepted\":false}");
        }
#endif
#if CSP_BLE_ALLOC_TRACE
        else if(!std::strcmp(serial_input,"TRACE_STATUS")){
          const auto t=ble_alloc_trace_summary();
          USBSerial.printf("{\"snapshot\":%lu,\"phase\":%u,\"allocations\":%lu,\"duplicates\":%lu,\"overflow\":%lu,\"unknown_frees\":%lu,\"failed\":%lu,\"at_stop\":%lu,\"live\":%lu,\"live_bytes\":%llu}\n",
            (unsigned long)t.snapshot,unsigned(t.phase),(unsigned long)t.allocations,(unsigned long)t.duplicates,
            (unsigned long)t.overflow,(unsigned long)t.unknown_frees,(unsigned long)t.failed,
            (unsigned long)t.at_stop,(unsigned long)t.live,(unsigned long long)t.live_bytes);
        }else if(!std::strcmp(serial_input,"TRACE_SPAN")){
          const auto h=ble_alloc_trace_heap_span();
          USBSerial.printf("{\"captured\":%s,\"heap_start\":%lu,\"heap_end\":%lu,\"before_start\":%lu,\"before_bytes\":%u,\"after_start\":%lu,\"after_bytes\":%u,\"global_after_start\":%lu,\"global_after_bytes\":%u,\"overflow\":%lu,\"barriers\":[",
            h.captured?"true":"false",(unsigned long)h.heap_start,(unsigned long)h.heap_end,(unsigned long)h.before_start,unsigned(h.before_bytes),
            (unsigned long)h.after_start,unsigned(h.after_bytes),(unsigned long)h.global_after_start,unsigned(h.global_after_bytes),(unsigned long)h.overflow);
          for(uint32_t i=0;i<h.barriers;++i)USBSerial.printf("%s{\"address\":%lu,\"bytes\":%u}",i?",":"",(unsigned long)h.retained[i].address,unsigned(h.retained[i].bytes));
          USBSerial.println("]}");
        }else if(!std::strncmp(serial_input,"TRACE_PAGE ",11)){
          unsigned offset=0;char extra=0;
          if(sscanf(serial_input+11,"%u %c",&offset,&extra)!=1||offset>256)USBSerial.println("{\"accepted\":false}");
          else{
            pro::trace::Record rows[4];const auto count=ble_alloc_trace_page(offset,rows,4);
            USBSerial.printf("{\"offset\":%u,\"records\":[",offset);
            for(size_t i=0;i<count;++i){const auto& r=rows[i];
              USBSerial.printf("%s{\"address\":%lu,\"caller\":%lu,\"bytes\":%u,\"sequence\":%lu,\"phase\":%u,\"api\":%u,\"live\":%s}",
                i?",":"",(unsigned long)r.address,(unsigned long)r.caller,unsigned(r.bytes),(unsigned long)r.sequence,unsigned(r.phase),unsigned(r.api),r.live?"true":"false");
            }
            USBSerial.println("]}");
          }
        }
#endif
#if CSP_RADIO_RELEASE_ENABLED
        else if(!std::strcmp(serial_input,"RADIO_STATUS")){
          USBSerial.printf("{\"release_pending\":%s,\"release_free\":%lu,\"release_largest\":%lu,\"release_wait_ms\":%lu,\"restart_attempted\":%s}\n",
            release_pending?"true":"false",(unsigned long)release_free,(unsigned long)release_largest,
            (unsigned long)release_wait_ms,restart_attempted?"true":"false");
        }
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
        else if(!std::strcmp(serial_input,"NVS_RELEASE_STATUS")){
          const auto n=wifi_setup_nvs_release_status();
          USBSerial.printf("{\"step\":%u,\"sdk_error\":%ld,\"flash_equal\":%s,\"before_largest\":%lu,\"after_largest\":%lu}\n",
            unsigned(n.step),static_cast<long>(n.sdk_error),n.flash_equal?"true":"false",
            static_cast<unsigned long>(n.before_largest),static_cast<unsigned long>(n.after_largest));
        }
#endif
#endif
        else if(!std::strcmp(serial_input,"BOOT_STATUS")){
          // Read-only RAM/SDK facts. No persistent writes or restart command.
          // A normal BLE timeout must not reset uptime or reset these counters.
          USBSerial.printf("{\"uptime_ms\":%lu,\"reset_reason\":%u}\n",
            static_cast<unsigned long>(millis()),unsigned(esp_reset_reason()));
        }
#if CSP_PANIC_DIAGNOSTICS
        else if(!std::strcmp(serial_input,"PANIC_STATUS"))panicStatus();
#endif
        else if(!std::strcmp(serial_input,"OWNER_STATUS")){
          const uint32_t now=millis();const bool down=digitalRead(0)==LOW;
          const uint32_t blocked=ownerBlocked(now,down);
          USBSerial.printf("{\"owner_state\":%u,\"boot_down\":%s,\"held_ms\":%lu,\"blocked\":%lu,\"can_enroll\":%s,\"last_blocked\":%lu,\"install_step\":%u}\n",
            unsigned(owner_state),down?"true":"false",static_cast<unsigned long>(ownerHeldMs(now,down)),
            static_cast<unsigned long>(blocked),blocked?"false":"true",static_cast<unsigned long>(last_owner_blocked),unsigned(wifi_owner_install_step()));
        }else if(!std::strcmp(serial_input,"OWNER_ID")){
          uint8_t mac[6]{};if(esp_read_mac(mac,ESP_MAC_BT)==ESP_OK)USBSerial.printf("{\"owner_device\":\"%02x%02x%02x%02x%02x%02x\"}\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        }else if(serial_size==14+OwnerRecordSize*2&&!std::memcmp(serial_input,"OWNER_INSTALL ",14)){
          last_owner_blocked=ownerBlocked(millis(),digitalRead(0)==LOW);
          bool ok=last_owner_blocked==0;
          std::array<uint8_t,OwnerRecordSize> bytes{};
          for(size_t n=0;ok&&n<bytes.size();++n){unsigned value=0;for(unsigned k=0;k<2;++k){char h=serial_input[14+n*2+k];
            unsigned digit=h>='0'&&h<='9'?unsigned(h-'0'):h>='a'&&h<='f'?unsigned(h-'a'+10):16;if(digit>15){ok=false;break;}value=value*16+digit;}bytes[n]=value;}
          if(ok){
            ok=wifi_owner_first_install(bytes.data(),bytes.size());
            const auto loaded_owner=wifi_owner_load(owner);
            owner_state=ok?loaded_owner:loaded_owner==WifiOwnerLoad::Missing?WifiOwnerLoad::Missing:WifiOwnerLoad::Fault;
            if(ok){enrollment_wait_release=true;display="Owner key saved\nRelease BOOT";}
            else if(owner_state==WifiOwnerLoad::Fault)display="Owner key needs review";
          }
          eraseSecretMemory(bytes.data(),bytes.size());
          USBSerial.println(ok?"{\"owner_installed\":true}":"{\"owner_installed\":false}");
        }else USBSerial.println("{\"accepted\":false}");
      }
      eraseSecretMemory(serial_input,sizeof serial_input);serial_size=0;serial_overflow=false;
    }else if(uint8_t(c)<32||uint8_t(c)>126||serial_size>=sizeof(serial_input)-1)serial_overflow=true;
    else if(!serial_overflow)serial_input[serial_size++]=c;
  }
}
}

// Setup-only targets omit the production worker. The source-only link probe
// retains its real implementation for size measurements but never starts it.
#if !CSP_BACKUP_LINK_PROBE && !CSP_ENABLE_BACKUP_SETUP
BackupStatus backup_status(){BackupStatus s;s.message[0]=0;return s;}
void backup_message(const char*,int){}
void set_maintenance(bool enabled){setup_paused=enabled;}
bool maintenance_active(){return setup_paused;}
#endif
#if CSP_ENABLE_OWNER_LOGIN
std::mutex& wifi_setup_owner_login_output_mutex(){return login_output_mutex;}
bool wifi_setup_owner_login_info_allowed(){return !login_barrier.load();}
bool wifi_setup_owner_login_output_allowed(const WifiSetupSession& session,const uint8_t* input,size_t size,uint32_t& ack_ticket){
  ack_ticket=0;if(!login_barrier.load())return true;
  constexpr char identity[]="{\"v\":2,\"id\":";constexpr size_t prefix=sizeof identity-1;
  if(!input||size<=prefix||std::memcmp(input,identity,prefix))return false;
  size_t at=prefix;uint32_t id=0;unsigned digits=0;
  while(at<size&&input[at]>='0'&&input[at]<='9'){
    const auto digit=unsigned(input[at++]-'0');if(++digits>10||id>(0x7fffffffU-digit)/10)return false;id=id*10+digit;
  }
  if(!id||!digits||at>=size||input[at]!=',')return false;
  std::lock_guard<std::mutex> lock(mailbox_mutex);
  if(!login_handoff.matches(session.id,session.generation,mailbox.job)||!same(mailbox.session,session))return false;
  char header[128]{};
  const int pending=std::snprintf(header,sizeof header,"{\"v\":2,\"id\":%lu,\"ok\":true,\"job\":%lu,\"pending\":true}",
    static_cast<unsigned long>(id),static_cast<unsigned long>(mailbox.job));
  if(pending>0&&size==size_t(pending)&&!std::memcmp(input,header,size))return true;
  const int bytes=std::snprintf(header,sizeof header,"{\"v\":2,\"id\":%lu,\"ok\":true,\"job\":%lu,\"pending\":false,\"data\":",
    static_cast<unsigned long>(id),static_cast<unsigned long>(mailbox.job));
  if(mailbox.state!=Mailbox::State::Done||bytes<=0||size_t(bytes)>=sizeof header||
     size!=size_t(bytes)+mailbox.output_size+1||std::memcmp(input,header,size_t(bytes))||
     std::memcmp(input+bytes,mailbox.output.data(),mailbox.output_size)||input[size-1]!='}')return false;
  ack_ticket=mailbox.job;return true;
}
void wifi_setup_owner_login_ack_encrypted(const WifiSetupSession& session,uint32_t ticket){
  if(!ticket)return;const auto now=millis();std::lock_guard<std::mutex> lock(mailbox_mutex);
  login_handoff.encrypted(session.id,session.generation,ticket,now);
}
#endif
bool wifi_setup_owner_parameters(protocomm_security2_params_t& p){
  if(owner_state!=WifiOwnerLoad::Ready
#if CSP_ENABLE_OWNER_LOGIN
     ||login_barrier.load()||login_fault
#endif
     )return false;
  p={reinterpret_cast<const char*>(owner.salt.data()),owner.salt.size(),
     reinterpret_cast<const char*>(owner.verifier.data()),owner.verifier.size()};return true;
}
esp_err_t wifi_setup_endpoint(const WifiSetupSession& session,const WireRequest& r,const uint8_t* input,size_t n,uint8_t** out,ssize_t* length){
  if(out)*out=nullptr;if(length)*length=0;
  if(!out||!length||!input||!n||n>WifiWireLimit||!ble_management_authorizes(session))return ESP_ERR_INVALID_STATE;
  *out=nullptr;*length=0;
  auto* response=static_cast<char*>(std::malloc(pro::ble::ResponseLimit));if(!response)return ESP_ERR_NO_MEM;
  int size=0;
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex);
#if CSP_ENABLE_OWNER_LOGIN
    if(login_barrier.load()&&(r.op!=Op::Result||!login_handoff.matches(session.id,session.generation,r.job)))
      size=snprintf(response,pro::ble::ResponseLimit,"{\"v\":2,\"id\":%lu,\"ok\":false,\"error\":\"reconnect_required\"}",static_cast<unsigned long>(r.id));
    else
#endif
    if(r.op==Op::Result){
      if(!same(mailbox.session,session)||mailbox.job!=r.job||mailbox.state==Mailbox::State::Empty)
        size=snprintf(response,pro::ble::ResponseLimit,"{\"v\":2,\"id\":%lu,\"ok\":false,\"error\":\"stale_job\"}",static_cast<unsigned long>(r.id));
      else if(mailbox.state!=Mailbox::State::Done)
        size=snprintf(response,pro::ble::ResponseLimit,"{\"v\":2,\"id\":%lu,\"ok\":true,\"job\":%lu,\"pending\":true}",static_cast<unsigned long>(r.id),static_cast<unsigned long>(r.job));
      else{
        size=snprintf(response,pro::ble::ResponseLimit,"{\"v\":2,\"id\":%lu,\"ok\":true,\"job\":%lu,\"pending\":false,\"data\":",static_cast<unsigned long>(r.id),static_cast<unsigned long>(r.job));
        if(size>0&&size+mailbox.output_size+2<pro::ble::ResponseLimit){
          std::memcpy(response+size,mailbox.output.data(),mailbox.output_size);size+=mailbox.output_size;response[size++]='}';response[size]=0;
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
           if(r.job==public_handoff_job.load())public_result_returned.store(true);
#endif
        }else size=-1;
      }
    }else if(mailbox.state==Mailbox::State::Queued||mailbox.state==Mailbox::State::Running||mailbox.next_job==UINT32_MAX)
      size=snprintf(response,pro::ble::ResponseLimit,"{\"v\":2,\"id\":%lu,\"ok\":false,\"error\":\"busy\"}",static_cast<unsigned long>(r.id));
    else{
      eraseSecretMemory(mailbox.output.data(),mailbox.output.size());mailbox.output_size=0;
      mailbox.session=session;mailbox.job=mailbox.next_job++;mailbox.input_size=n;
      std::memcpy(mailbox.input.data(),input,n);mailbox.state=Mailbox::State::Queued;
      size=snprintf(response,pro::ble::ResponseLimit,"{\"v\":2,\"id\":%lu,\"ok\":true,\"job\":%lu,\"pending\":true}",static_cast<unsigned long>(r.id),static_cast<unsigned long>(mailbox.job));
    }
  }
  if(size<=0||size>=int(pro::ble::ResponseLimit)){std::free(response);return ESP_ERR_INVALID_SIZE;}
  *out=reinterpret_cast<uint8_t*>(response);*length=size;return ESP_OK;
}
void wifi_setup_runtime_setup(){
#if CSP_PANIC_DIAGNOSTICS
  panic_diagnostics_begin();
#endif
  delay(500);pinMode(0,INPUT_PULLUP);lcd_init();lcd_show_loading();
#if CSP_BACKUP_LINK_PROBE || CSP_ENABLE_BACKUP_SETUP
  // Both modes start paused. The link probe never starts a worker; combined
  // mode admits it only through the owner loop after valid Wi-Fi/configuration.
  set_maintenance(true);
#endif
#if CSP_ENABLE_BACKUP_SETUP
  backup_memory_init();
#endif
  const bool prepared=ble_platform_prepare();(void)ble_management_status();clearMailbox();
  owner_state=wifi_owner_load(owner);
  StoredNetworks saved;auto loaded=store.load(saved);
  if(loaded==Stored::Missing)loaded=wifi_import_legacy_network(saved);
  if(loaded==Stored::Valid)store_fault=!model.loadTrusted(saved);
  else if(loaded!=Stored::Missing)store_fault=true;
  eraseSecretMemory(&saved,sizeof saved);
  radio_ready=prepared&&wifi_setup_radio_init();
  init_usb_storage(false);USBSerial.setTxTimeoutMs(100);
  if(radio_ready&&!store_fault)restoreActive();
  if(!prepared||!radio_ready)display="Wi-Fi/BLE init failed";
  else if(store_fault)display="Settings need review";
  else if(owner_state==WifiOwnerLoad::Fault)display="Owner key needs review";
  else if(owner_state!=WifiOwnerLoad::Ready)display="Owner card required\nSD out; hold 10s";
#if CSP_ENABLE_BACKUP_SETUP
  // Cloud configuration is read only. Do not keep its temporary parsing or
  // account/CA allocations across later Bluetooth setup windows.
  {
    auto config=load_backup_config();backup_config_valid=config.is_valid;
    if(config.is_valid){
      const size_t length=config.ntp_server.length();
      if(!length||length>=sizeof time_server)backup_config_valid=false;
      else{
        std::memcpy(time_server,config.ntp_server.c_str(),length);time_server[length]=0;
        configTzTime(config.timezone.c_str(),time_server);
      }
    }
  }
  if(!backup_config_valid)display="Cloud setup unavailable\nFiles stay on USB";
#endif
}
void wifi_setup_runtime_loop(){
  poll_storage();
  const uint32_t now=millis();const bool down=digitalRead(0)==LOW;
#if CSP_ENABLE_BACKUP_SETUP
  backup_memory_poll(now);
#endif
  if(down&&!button_down)button_down=now;if(!down)button_down=0;
  serial();
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  if(!publicOwnsStation())
#endif
  wifi_setup_radio_poll();ble_management_set_connection_status(WiFi.status()==WL_CONNECTED);
  ble_management_poll();
#if CSP_BLE_ALLOC_TRACE
  ble_alloc_trace_poll(now);
#endif
  reapMailbox();
#if CSP_ENABLE_OWNER_LOGIN
  loginTick(millis());
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP
  cloudTick(millis());
#endif
  if(transaction_session.generation){
    Snapshot s;const auto a=access(transaction_session);
    if(!Candidate::authorized(a)||model.snapshot(a,now,true,s)!=Error::None)endTransaction();
  }
  if(check_pending){
    const auto& radio=wifi_setup_radio_snapshot();
    if(!ble_management_authorizes(check_session)){endTransaction();}
    else if(radio.state==WifiSetupRadioState::LocalConnected||radio.state==WifiSetupRadioState::Failed||radio.state==WifiSetupRadioState::Fault){
      const auto e=radio.state==WifiSetupRadioState::LocalConnected?Error::LocalOnly:radio.error;
      model.finishCheck(access(check_session),checking,millis(),e);check_pending=false;
      if(e!=Error::LocalOnly)restoreActive();
    }
  }
  worker();
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  // Mailbox dispatch may have entered a coordinator phase using a newer
  // millis() than this loop's original sample. Poll that new phase afterward.
  publicTick(millis());
#endif
  if(enrollment_wait_release&&!down)enrollment_wait_release=false;
  if(owner_state==WifiOwnerLoad::Ready&&radio_ready&&ble_platform_ready()&&!enrollment_wait_release){
    if(
#if CSP_ENABLE_OWNER_LOGIN
       !login_barrier.load()&&!login_fault&&
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
       (!publicBlocks()||public_coordinator.prepared())&&
#endif
       lifecycle.button(down,now)==pro::BleLifecycle::Action::Pause){
#if CSP_ENABLE_BACKUP_SETUP
      pauseBackup();
#endif
      display="Pausing safely\nUSB stays available";
    }
    const auto radio_state=wifi_setup_radio_snapshot().state;
    bool network_busy=radio_state==WifiSetupRadioState::Connecting||radio_state==WifiSetupRadioState::Scanning||radio_state==WifiSetupRadioState::Cancelling;
#if CSP_ENABLE_LOCAL_DASHBOARD
    if(lifecycle.state()!=pro::BleLifecycle::State::Idle){
      dashboard_stop_failed=!pro::local_dashboard::stop();
      if(dashboard_stop_failed)network_busy=true;
    }
#endif
#if CSP_ENABLE_BACKUP_SETUP
    if(lifecycle.state()==pro::BleLifecycle::State::Waiting&&!backupReclaimed())network_busy=true;
#endif
     auto action=pro::BleLifecycle::Action::None;
#if CSP_ENABLE_OWNER_LOGIN
     if(login_close_requested){login_close_requested=false;action=lifecycle.requestClose();}
     else
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
     if(public_close_requested){public_close_requested=false;action=lifecycle.requestClose();}
     else
#endif
#if CSP_USB_BENCH_AUTOMATION
     if(bench_close_queued){bench_close_queued=false;action=lifecycle.requestClose();}
     else
#endif
       // serial() may have opened a window with a newer millis sample. Poll it
       // afterward rather than subtracting from this loop's older timestamp.
       action=lifecycle.poll(millis(),network_busy,ble_management_active()&&
         (!pro::setup_session::Guided||!ble_management_status().teardown_failed),freeInternal(),largestInternal());
    if(action==pro::BleLifecycle::Action::Start){
      before_free=freeInternal();before_largest=largestInternal();
#if CSP_RADIO_RELEASE_ENABLED
      release_pending=restart_attempted=false;release_started=release_free=release_largest=release_wait_ms=0;
#endif
      after_free=after_largest=0;recovery_checked=recovery_restored=false;
#if CSP_BLE_ALLOC_TRACE
       ble_alloc_trace_begin();
#endif
      const bool ok=wifi_setup_radio_snapshot().state!=WifiSetupRadioState::Fault&&
        pro::BleLifecycle::canStart(before_free,before_largest)&&ble_management_begin();
      lifecycle.started(ok);display=ok?"Wi-Fi setup\nConnect from phone":"BLE could not start";
      if(!ok)action=pro::BleLifecycle::Action::Stop;
    }
    if(action==pro::BleLifecycle::Action::Stop&&!closing){
#if CSP_ENABLE_BACKUP_SETUP
      if(!backupReclaimed()){
        backup_bridge_fault=true;lifecycle.stopped(false,false);
        display="Backup pause timed out\nFiles stay on USB";
      }else
#endif
      {
#if CSP_RADIO_RELEASE_ENABLED
      endTransaction(false);clearMailbox();
      bool stopped=ble_management_stop();
      // Restore Wi-Fi only after verified BLE stop and full driver release.
      // Unknown release/reinit must stay blocked, never reset host-owned USB.
      if(stopped)stopped=wifi_setup_radio_release();
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
      if(stopped)stopped=wifi_setup_nvs_release_cache();
#endif
      release_pending=stopped;release_started=millis();
#else
      endTransaction();clearMailbox();const bool stopped=ble_management_stop();
#endif
      closing=true;close_stop_verified=stopped;close_started=millis();
      display="Finishing setup\nUSB stays available";
      }
    }
#if CSP_RADIO_RELEASE_ENABLED
    if(closing&&release_pending){
      release_free=freeInternal();release_largest=largestInternal();
      release_wait_ms=uint32_t(millis()-release_started);
      // SDK timer destruction is queued. Keep servicing USB/owner-loop work
      // while allowing it to drain; never start Wi-Fi in a fragmented arena.
      const bool recovered=release_largest>=before_largest;
#if CSP_ENABLE_BLE_RELEASE_SETTLEMENT
      // An authenticated setup session can drain beyond two seconds. Use the
      // existing overall closing budget, without extending it or retrying an
      // unverified stop. A late allocator observation cannot reopen a failure.
      const bool expired=uint32_t(millis()-close_started)>=CloseSettlementMs;
      if((release_wait_ms>=500&&recovered)||expired){
        release_pending=false;
        if(recovered&&!expired){restart_attempted=true;radio_ready=wifi_setup_radio_init();close_stop_verified=radio_ready;if(radio_ready)restoreActive();}
        else close_stop_verified=false;
      }
#else
      if((release_wait_ms>=500&&recovered)||release_wait_ms>=2000){
        release_pending=false;
        if(recovered){restart_attempted=true;radio_ready=wifi_setup_radio_init();close_stop_verified=radio_ready;if(radio_ready)restoreActive();}
        else close_stop_verified=false;
      }
#endif
    }
    if(closing&&!release_pending){
#else
    if(closing){
#endif
      const auto current=wifi_setup_radio_snapshot().state;
      const bool settled=current!=WifiSetupRadioState::Connecting&&current!=WifiSetupRadioState::Scanning&&current!=WifiSetupRadioState::Cancelling;
      after_free=freeInternal();after_largest=largestInternal();
      const bool restored=settled&&current!=WifiSetupRadioState::Fault&&
        pro::BleLifecycle::restored(before_free,before_largest,after_free,after_largest);
      if(!close_stop_verified||restored||uint32_t(millis()-close_started)>=CloseSettlementMs){
        recovery_checked=true;recovery_restored=restored;
        bool closed=lifecycle.stopped(close_stop_verified,restored);closing=false;
#if CSP_ENABLE_OWNER_LOGIN
        if(closed&&login_barrier.load()&&!loginReload()){
          login_fault=true;backup_bridge_fault=true;set_maintenance(true);owner_state=WifiOwnerLoad::Fault;closed=false;
        }
#endif
#if CSP_BLE_ALLOC_TRACE
         // Keep observing network restoration through the complete settlement
         // interval, then freeze metadata after the existing bounded delay.
         ble_alloc_trace_stop(millis());
#endif
        display=closed?"Wi-Fi setup\nHold BOOT 3s":"BLE needs review";
#if CSP_ENABLE_OWNER_LOGIN
        if(login_fault)display="Owner login needs review\nFiles stay on USB";
#endif
      }
    }
  }
#if CSP_ENABLE_BACKUP_SETUP
  recoverWifi(now);
  resumeBackup(now);
#endif
#if CSP_ENABLE_LOCAL_DASHBOARD
  // Only the existing owner loop samples sanitized RAM status. The HTTP path
  // never reads files, receipts, configuration or private owner material.
  // Closing happens before pauseBackup's worker/radio handoff and is proved
  // again here; no listener may remain in a BLE or guest-routing phase.
  const auto dashboard_status=backup_status();
  pro::local_dashboard::Snapshot dashboard;
  dashboard.firmware=FirmwareVersion;dashboard.uptime=now;
  dashboard.wifi=WiFi.status()==WL_CONNECTED;dashboard.media=storage_media_detected();
  dashboard.active=dashboard_status.active;dashboard.scanning=dashboard_status.scanning;
  dashboard.has_scan=dashboard_status.hasScan;dashboard.scan_complete=dashboard_status.scanComplete;
  dashboard.window_open=backup_window_open();dashboard.paused=maintenance_active();dashboard.cloud=dashboard_status.cloud;dashboard.configured=backup_config_valid;
  dashboard.verified=dashboard_status.verified;dashboard.pending=dashboard_status.pending;dashboard.failed=dashboard_status.failed;
  dashboard.done=dashboard_status.transferDone;dashboard.total=dashboard_status.transferTotal;dashboard.speed=dashboard_status.transferSpeed;
  dashboard.quiet=storage_quiet_remaining(backup_policy().idleSeconds);dashboard.last_backup=dashboard_status.lastBackup;
  dashboard.error=dashboard_status.error;dashboard.percent=dashboard_status.progress;dashboard.phase=uint8_t(dashboard_status.phase);
  const bool dashboard_allowed=owner_state==WifiOwnerLoad::Ready&&!store_fault&&!backup_bridge_fault&&!dashboard_stop_failed&&radio_ready&&
    !closing&&lifecycle.state()==pro::BleLifecycle::State::Idle&&!ble_management_active()&&
    wifi_setup_radio_snapshot().state==WifiSetupRadioState::LocalConnected
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
    &&!publicBlocks()
#endif
    ;
  // Prioritize backup memory; a browser may retry after a constrained TLS peak.
  pro::local_dashboard::poll(now,dashboard_allowed,uint32_t(WiFi.localIP()),
    freeInternal()>=(pro::local_dashboard::listening()?48u:64u)*1024&&largestInternal()>=16*1024,dashboard);
#endif
  const char* screen=display;
  if(owner_state==WifiOwnerLoad::Missing&&down){
    const auto blocked=ownerBlocked(now,down);
    screen=blocked==0?"Ready for owner key\nKeep BOOT held":
      blocked==OwnerHoldShort?"Keep BOOT held\n10 seconds needed":"Owner setup blocked\nCheck USB status";
  }
  if(ble_management_authenticated()
#if CSP_ENABLE_OWNER_LOGIN
     &&!login_barrier.load()&&!login_draft
#endif
     ){
    const auto s=wifi_setup_radio_snapshot().state;
    screen=s==WifiSetupRadioState::Scanning?"Searching Wi-Fi":s==WifiSetupRadioState::Connecting?"Joining Wi-Fi...":
      s==WifiSetupRadioState::Failed?"Wi-Fi check failed":s==WifiSetupRadioState::LocalConnected?
      "Wi-Fi connected\nInternet unverified":"Owner authenticated\nChoose Wi-Fi";
  }
#if CSP_ENABLE_OWNER_LOGIN
  if(login_draft)screen="Owner login update\nFinish on phone";
#endif
  if(uint32_t(now-last_draw)>=500){
#if CSP_ENABLE_BACKUP_SETUP
    if(lifecycle.state()==pro::BleLifecycle::State::Idle&&!backup_bridge_fault&&backup_config_valid&&
       backup_worker_snapshot().phase==BackupWorkerPhase::Running){
      last_draw=now;last_lcd=nullptr;
      const auto status=backup_status();lcd_pro_update(status,storage_owner(),WiFi.status()==WL_CONNECTED);
    }else
#endif
    if(!last_lcd||std::strcmp(last_lcd,screen)){last_draw=now;last_lcd=screen;lcd_show_text(screen);}
  }
  delay(5);
}
#endif
