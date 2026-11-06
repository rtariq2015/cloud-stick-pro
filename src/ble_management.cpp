#include "ble_management.h"
#include "owner_cloud_feature.h"
#include "owner_login_feature.h"
#if CSP_ENABLE_BLE_MANAGEMENT

#if !CSP_ENABLE_WIFI_SETUP && (!defined(CSP_BLE_SYNTHETIC_ONLY) || CSP_BLE_SYNTHETIC_ONLY != 1)
#error Milestone 1 BLE may only be compiled with synthetic test credentials
#endif

#include "pro_ble_message.h"
#include "pro_setup_session.h"
#include "pro_device.h"
#include "pro_event_log.h"
#include "file_history.h"
#include "ble_sdk_lifecycle.h"
#include <Arduino.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <protocomm.h>
#include <protocomm_ble.h>
#include <protocomm_security2.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#if CSP_ENABLE_WIFI_SETUP
#include "wifi_setup_runtime.h"
#include <wifi_wire.h>
#endif

#if !CONFIG_BT_NIMBLE_ENABLED || !CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2
#error The pinned NimBLE and protocomm Security 2 SDK support is required
#endif
#if CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION != 1
#error Revalidate the phone client before changing the Security 2 patch
#endif

namespace {
constexpr uint32_t WindowMs=pro::setup_session::AdvertisingMs, HandshakeMs=pro::setup_session::HandshakeMs,
  IdleMs=pro::setup_session::AuthenticatedIdleMs;
constexpr size_t RuntimeFreeFloor=32768, RuntimeBlockFloor=16384;
constexpr uint32_t NoSession=0xffffffffU;
// Test identity only: the public Espressif CC0 provisioning example uses
// username "wifiprov", password "abcd1234". These are not owner credentials.
// https://github.com/espressif/esp-idf/blob/v5.5.5/examples/provisioning/wifi_prov_mgr/main/app_main.c
#if !CSP_ENABLE_WIFI_SETUP
static const uint8_t sec2_salt[] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8, 0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4
};

static const uint8_t sec2_verifier[] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba
};
#endif

protocomm_t* instance=nullptr;
// SDK callbacks run in task context. This mutex protects only bounded state
// copies/updates: never call the SDK, millis(), allocators or radio teardown
// while holding it. Individual atomics remain useful for cheap UI telemetry;
// policy decisions always use the coherent snapshot under stateMutex.
std::mutex stateMutex;
std::atomic<bool> active{false}, authenticated{false}, connected{false}, stopRequested{false}, wifiConnected{false}, stopping{false}, teardownFailed{false};
std::atomic<int32_t> lastError{0};
std::atomic<uint32_t> session{NoSession}, connectedAt{0}, lastActivity{0}, handshakeCalls{0};
uint32_t openedAt=0,windowGeneration=0;
uint64_t stateRevision=0,sessionGeneration=0;
bool sessionOpening=false;
pro::ble::RequestSequence sequence;
char deviceId[24]{}, deviceName[24]{};

esp_err_t rejectLocked(esp_err_t error) {
  // Terminal for this physical window, including the gap before owner-task
  // teardown. Preserve its first cause when later traffic is denied.
  if(!stopRequested.load()) {
    lastError.store(error);stopRequested.store(true);++stateRevision;
  }
  return error;
}
esp_err_t reject(esp_err_t error) {
  std::lock_guard<std::mutex> lock(stateMutex);return rejectLocked(error);
}
bool admittedLocked(uint32_t id,uint64_t generation) {
  return active.load() && !stopping.load() && !stopRequested.load() &&
    !teardownFailed.load() && !sessionOpening && session.load()==id &&
    id!=NoSession && sessionGeneration==generation;
}
bool timelyLocked(uint32_t now){
  if(!pro::setup_session::Guided)return true;
  const bool expired=(!authenticated.load()&&pro::ble::elapsedAtLeast(now,openedAt,WindowMs))||
    (connected.load()&&!authenticated.load()&&pro::ble::elapsedAtLeast(now,connectedAt.load(),HandshakeMs))||
    (authenticated.load()&&pro::ble::elapsedAtLeast(now,lastActivity.load(),IdleMs));
  if(expired)rejectLocked(ESP_ERR_TIMEOUT);
  return !expired;
}
bool admittedAtLocked(uint32_t id,uint64_t generation,uint32_t now){return admittedLocked(id,generation)&&timelyLocked(now);}
bool validOutputs(uint8_t** outbuf,ssize_t* outlen) {
  if(!outbuf || !outlen) return false;
  *outbuf=nullptr;*outlen=0;return true;
}
esp_err_t finishSdkOutput(esp_err_t result,uint32_t id,uint64_t generation,
    uint8_t** outbuf,ssize_t* outlen) {
  const uint32_t now=pro::setup_session::Guided?millis():0;
  bool discard=false;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(result!=ESP_OK) rejectLocked(result);
    if(!admittedAtLocked(id,generation,now)) {
      discard=true;
      if(result==ESP_OK) result=ESP_ERR_INVALID_STATE;
    }
  }
  if(discard) {free(*outbuf);*outbuf=nullptr;*outlen=0;}
  return result;
}
esp_err_t newSession(protocomm_security_handle_t handle,uint32_t id) {
  const uint32_t admittedAt=pro::setup_session::Guided?millis():0;
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(!active.load() || stopping.load() || stopRequested.load() ||
        teardownFailed.load() || sessionOpening || session.load()!=NoSession || id==NoSession||!timelyLocked(admittedAt))
      return rejectLocked(ESP_ERR_INVALID_STATE);
    sessionOpening=true;generation=++sessionGeneration;++stateRevision;
  }
  esp_err_t result=protocomm_security2.new_transport_session(handle,id);
  uint32_t now=millis();
  bool discard=false;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(result!=ESP_OK) {
      sessionOpening=false;return rejectLocked(result);
    }
    if(!active.load() || stopping.load() || stopRequested.load() ||
        sessionGeneration!=generation||!timelyLocked(now)) {
      sessionOpening=false;discard=true;
    }else{
      sequence.close();
      if(!sequence.open(id)) {
        sessionOpening=false;rejectLocked(ESP_ERR_INVALID_STATE);discard=true;
      }else{
        // Publish the entire connection together after sampling the callback
        // time. Poll never observes connected with an uninitialized timestamp.
        connectedAt.store(now);lastActivity.store(now);handshakeCalls.store(0);
        session.store(id);authenticated.store(false);connected.store(true);
        sessionOpening=false;++stateRevision;
      }
    }
  }
  if(discard) {
    // Cleanup is permitted after denial; it never clears the terminal latch.
    protocomm_security2.close_transport_session(handle,id);
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}
esp_err_t closeSession(protocomm_security_handle_t handle,uint32_t id) {
  // Transport cleanup must remain callable after a terminal rejection.
  esp_err_t result=protocomm_security2.close_transport_session(handle,id);
  std::lock_guard<std::mutex> lock(stateMutex);
  if(session.load()==id) {
    sequence.close();session.store(NoSession);connected.store(false);
    authenticated.store(false);handshakeCalls.store(0);
    ++sessionGeneration;++stateRevision;
    if(pro::setup_session::Guided){
      // A peer disconnect closes its setup window; it is not a new invitation
      // to use the same physical window. Cleanup/resume still needs owner proof.
      if(result!=ESP_OK)rejectLocked(result);
      else stopRequested.store(true);
    }
  }
  return result;
}
esp_err_t secureHandshake(protocomm_security_handle_t handle,const void* params,uint32_t id,
    const uint8_t* inbuf,ssize_t inlen,uint8_t** outbuf,ssize_t* outlen,void* priv) {
  if(!validOutputs(outbuf,outlen)) return reject(ESP_ERR_INVALID_ARG);
  const uint32_t now=pro::setup_session::Guided?millis():0;
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(stateMutex);generation=sessionGeneration;
    if(!admittedAtLocked(id,generation,now) || !inbuf || inlen<=0 ||
        !pro::ble::handshakeFrameSize(size_t(inlen)) || authenticated.load() ||
        handshakeCalls.load()>=2) return rejectLocked(ESP_ERR_INVALID_ARG);
    handshakeCalls.fetch_add(1);++stateRevision;
  }
  esp_err_t result=protocomm_security2.security_req_handler(handle,params,id,inbuf,inlen,outbuf,outlen,priv);
  return finishSdkOutput(result,id,generation,outbuf,outlen);
}
esp_err_t decrypt(protocomm_security_handle_t handle,uint32_t id,const uint8_t* inbuf,
    ssize_t inlen,uint8_t** outbuf,ssize_t* outlen) {
  if(!validOutputs(outbuf,outlen)) return reject(ESP_ERR_INVALID_ARG);
  const uint32_t now=pro::setup_session::Guided?millis():0;
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(stateMutex);generation=sessionGeneration;
    if(!admittedAtLocked(id,generation,now) || !inbuf || inlen<=0 ||
        !pro::ble::encryptedFrameSize(size_t(inlen))) return rejectLocked(ESP_ERR_INVALID_ARG);
  }
  // The SDK alone checks the authenticated session, GCM tag and patched counter.
  esp_err_t result=protocomm_security2.decrypt(handle,id,inbuf,inlen,outbuf,outlen);
  return finishSdkOutput(result,id,generation,outbuf,outlen);
}
esp_err_t encrypt(protocomm_security_handle_t handle,uint32_t id,const uint8_t* inbuf,
    ssize_t inlen,uint8_t** outbuf,ssize_t* outlen) {
  if(!validOutputs(outbuf,outlen)) return reject(ESP_ERR_INVALID_ARG);
  const uint32_t now=pro::setup_session::Guided?millis():0;
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(stateMutex);generation=sessionGeneration;
    if(!admittedAtLocked(id,generation,now) || !authenticated.load() || !inbuf ||
        inlen<=0 || size_t(inlen)>pro::ble::ResponseLimit) return rejectLocked(ESP_ERR_INVALID_ARG);
  }
#if CSP_ENABLE_OWNER_LOGIN
  // Serialize this bounded pure crypto publication with the owner's prewrite
  // barrier. Do not hold stateMutex; NVS/SDK teardown never holds this lease.
  std::unique_lock<std::mutex> publication(wifi_setup_owner_login_output_mutex());
  uint32_t ack_ticket=0;char denied[128]{};
  if(!wifi_setup_owner_login_output_allowed({id,generation},inbuf,size_t(inlen),ack_ticket)){
    unsigned version=2;uint32_t request=0;size_t at=12;
    if(inlen>12&&!std::memcmp(inbuf,"{\"v\":",5)&&(inbuf[5]=='1'||inbuf[5]=='2')&&!std::memcmp(inbuf+6,",\"id\":",6)){
      version=unsigned(inbuf[5]-'0');unsigned digits=0;
      while(at<size_t(inlen)&&inbuf[at]>='0'&&inbuf[at]<='9'){
        const auto digit=unsigned(inbuf[at++]-'0');if(++digits>10||request>(0x7fffffffU-digit)/10){request=0;break;}request=request*10+digit;
      }
      if(at>=size_t(inlen)||inbuf[at]!=',')request=0;
    }
    const int n=std::snprintf(denied,sizeof denied,"{\"v\":%u,\"id\":%lu,\"ok\":false,\"error\":\"reconnect_required\"}",version,static_cast<unsigned long>(request));
    if(n<=0||size_t(n)>=sizeof denied)return ESP_ERR_INVALID_SIZE;
    inbuf=reinterpret_cast<const uint8_t*>(denied);inlen=n;
  }
#endif
  esp_err_t result=protocomm_security2.encrypt(handle,id,inbuf,inlen,outbuf,outlen);
  result=finishSdkOutput(result,id,generation,outbuf,outlen);
#if CSP_ENABLE_OWNER_LOGIN
  if(result==ESP_OK&&ack_ticket)wifi_setup_owner_login_ack_encrypted({id,generation},ack_ticket);
#endif
  return result;
}
esp_err_t info(uint32_t id,const uint8_t* inbuf,ssize_t inlen,uint8_t** outbuf,
    ssize_t* outlen,void*) {
  if(!validOutputs(outbuf,outlen)) return reject(ESP_ERR_INVALID_ARG);
  pro::ble::Request request;
  if(inlen<=0 || !pro::ble::parseRequest(inbuf,size_t(inlen),request))
    return reject(ESP_ERR_INVALID_ARG);
  uint64_t generation;
  const uint32_t admittedAt=pro::setup_session::Guided?millis():0;
#if CSP_ENABLE_OWNER_LOGIN
  bool login_denied=false;
#endif
  {
    std::lock_guard<std::mutex> lock(stateMutex);generation=sessionGeneration;
    if(!admittedAtLocked(id,generation,admittedAt)) return rejectLocked(ESP_ERR_INVALID_STATE);
    if(!sequence.accept(id,request.id)) return rejectLocked(ESP_ERR_INVALID_ARG);
#if CSP_ENABLE_OWNER_LOGIN
    login_denied=!wifi_setup_owner_login_info_allowed();
#endif
    ++stateRevision;
  }
  char response[pro::ble::ResponseLimit];
#if CSP_ENABLE_OWNER_LOGIN
  if(login_denied){
    const int n=snprintf(response,sizeof response,"{\"v\":1,\"id\":%lu,\"ok\":false,\"error\":\"reconnect_required\"}",static_cast<unsigned long>(request.id));
    if(n<=0||size_t(n)>=sizeof response)return ESP_ERR_INVALID_SIZE;
    auto* output=static_cast<uint8_t*>(malloc(size_t(n)));if(!output)return ESP_ERR_NO_MEM;
    std::memcpy(output,response,size_t(n));*outbuf=output;*outlen=n;return ESP_OK;
  }
#endif
  int n=snprintf(response,sizeof response,
    "{\"v\":1,\"id\":%lu,\"ok\":true,\"model\":\"LILYGO T-Dongle S3\","
    "\"device_id\":\"%s\",\"firmware\":\"%s\",\"wifi_connected\":%s,"
    "\"backups_paused\":true,\"synthetic\":"
#if CSP_ENABLE_WIFI_SETUP
    "false}",
#else
    "true}",
#endif
    static_cast<unsigned long>(request.id),deviceId,FirmwareVersion,wifiConnected.load()?"true":"false");
  if(n<=0 || size_t(n)>=sizeof response) return reject(ESP_ERR_INVALID_SIZE);
  auto* output=static_cast<uint8_t*>(malloc(size_t(n)));
  if(!output) return reject(ESP_ERR_NO_MEM);
  memcpy(output,response,size_t(n));
  uint32_t now=millis();
  bool admitted;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    admitted=admittedAtLocked(id,generation,now)
#if CSP_ENABLE_OWNER_LOGIN
      &&wifi_setup_owner_login_info_allowed()
#endif
      ;
    if(admitted) {
      // Display/telemetry only: authorization was the preceding SDK decrypt.
      authenticated.store(true);lastActivity.store(now);++stateRevision;
      *outbuf=output;*outlen=n;
    }
  }
  if(!admitted) {
    free(output);
#if CSP_ENABLE_OWNER_LOGIN
    if(!wifi_setup_owner_login_info_allowed()){
      const int n=snprintf(response,sizeof response,"{\"v\":1,\"id\":%lu,\"ok\":false,\"error\":\"reconnect_required\"}",static_cast<unsigned long>(request.id));
      if(n<=0||size_t(n)>=sizeof response)return ESP_ERR_INVALID_SIZE;
      auto* denied=static_cast<uint8_t*>(malloc(size_t(n)));if(!denied)return ESP_ERR_NO_MEM;
      std::memcpy(denied,response,size_t(n));*outbuf=denied;*outlen=n;return ESP_OK;
    }
#endif
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}
#if CSP_ENABLE_WIFI_SETUP
esp_err_t wifi(uint32_t id,const uint8_t* inbuf,ssize_t inlen,uint8_t** outbuf,ssize_t* outlen,void*){
  if(!validOutputs(outbuf,outlen))return reject(ESP_ERR_INVALID_ARG);
  pro::wifi_setup::WireRequest request;
  if(inlen<=0||!pro::wifi_setup::parseWire(inbuf,size_t(inlen),request))return reject(ESP_ERR_INVALID_ARG);
  uint64_t generation;
  const uint32_t admittedAt=pro::setup_session::Guided?millis():0;
  {
    std::lock_guard<std::mutex> lock(stateMutex);generation=sessionGeneration;
    if(!admittedAtLocked(id,generation,admittedAt)||!authenticated.load())return rejectLocked(ESP_ERR_INVALID_STATE);
    if(!sequence.accept(id,request.id))return rejectLocked(ESP_ERR_INVALID_ARG);
    ++stateRevision;
  }
  const auto result=wifi_setup_endpoint({id,generation},request,inbuf,size_t(inlen),outbuf,outlen);
  const uint32_t now=millis();
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(admittedAtLocked(id,generation,now)){lastActivity.store(now);++stateRevision;}
  }
  return finishSdkOutput(result,id,generation,outbuf,outlen);
}
#endif
const protocomm_security_t& security() {
  static const protocomm_security_t selected=[] {
    protocomm_security_t bounded=protocomm_security2;
    bounded.new_transport_session=newSession;
    bounded.close_transport_session=closeSession;
    bounded.security_req_handler=secureHandshake;
    bounded.decrypt=decrypt;
    bounded.encrypt=encrypt;
    return bounded;
  }();
  return selected;
}
#if CSP_ENABLE_WIFI_SETUP
protocomm_security2_params_t parameters{};
#else
const protocomm_security2_params_t parameters{
  reinterpret_cast<const char*>(sec2_salt),sizeof sec2_salt,
  reinterpret_cast<const char*>(sec2_verifier),sizeof sec2_verifier
};
#endif
protocomm_ble_name_uuid_t endpoints[]{
  {"prov-session",0xff51},{"csp-info",0xff52},{"prov-version",0xff53}
#if CSP_ENABLE_WIFI_SETUP
  ,{"csp-wifi",0xff54}
#endif
};
constexpr uint8_t ServiceUuid[]{
  0xb4,0xdf,0x5a,0x1c,0x3f,0x6b,0xf4,0xbf,0xea,0x4a,0x82,0x03,0x04,0x90,0x1a,0x02
};
#if CSP_ENABLE_WIFI_SETUP
constexpr char VersionMetadata[]=
  "{\"prov\":{\"ver\":\"v1.1\",\"sec_ver\":2,\"sec_patch_ver\":1,\"cap\":[]},"
  "\"csp\":{\"milestone\":2,\"synthetic\":false,\"owner\":\"card\",\"wifi\":true"
#if CSP_ENABLE_OWNER_CLOUD_SETUP
  ",\"cloud\":true,\"cloud_protocol\":1"
#else
  ",\"cloud\":false"
#endif
#if CSP_ENABLE_OWNER_LOGIN
  ",\"owner_login\":true,\"owner_login_protocol\":1"
#endif
#if CSP_ENABLE_BACKUP_SETUP
  ",\"backup_status\":true"
#if CSP_ENABLE_FILE_HISTORY
  ",\"file_history\":true,\"file_history_protocol\":1"
#endif
#if CSP_ENABLE_EVENT_LOG
  ",\"event_log\":true,\"event_log_protocol\":1"
#endif
#endif
#if defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  ",\"public_wifi\":true,\"public_wifi_protocol\":1"
#endif
  "}}";
#else
constexpr char VersionMetadata[]=
  "{\"prov\":{\"ver\":\"v1.1\",\"sec_ver\":2,\"sec_patch_ver\":1,\"cap\":[]},"
  "\"csp\":{\"milestone\":1,\"synthetic\":true}}";
#endif
}

bool ble_management_begin() {
  if(instance || active.load()) return false;
#if CSP_ENABLE_WIFI_SETUP
  if(!wifi_setup_owner_parameters(parameters)){lastError.store(ESP_ERR_INVALID_STATE);return false;}
#endif
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    // Only the owner calls begin after a new physical hold. A disconnect or
    // close callback cannot reopen a rejected window.
    lastError.store(0);stopRequested.store(false);stopping.store(false);teardownFailed.store(false);
    ++windowGeneration;++sessionGeneration;++stateRevision;sessionOpening=false;
  }
  // SDK debug builds contain cryptographic hexdumps. Set their actual tags below
  // DEBUG before any handshake; application logging policy must retain this cap.
  esp_log_level_set("security2",ESP_LOG_WARN);
  esp_log_level_set("srp6a",ESP_LOG_WARN);
  esp_log_level_set("esp_srp",ESP_LOG_WARN);
  esp_log_level_set("protocomm",ESP_LOG_WARN);
  esp_log_level_set("protocomm_nimble",ESP_LOG_WARN);
  if(protocomm_security2.ver!=2 || protocomm_security2.patch_ver!=1) {
    lastError.store(ESP_ERR_INVALID_STATE);return false;
  }
  esp_err_t eventResult=esp_event_loop_create_default();
  if(eventResult!=ESP_OK && eventResult!=ESP_ERR_INVALID_STATE) {
    lastError.store(eventResult);return false;
  }
  uint8_t mac[6]{};
  if(esp_read_mac(mac,ESP_MAC_BT)!=ESP_OK) {
    lastError.store(ESP_ERR_INVALID_STATE);return false;
  }
#if CSP_ENABLE_WIFI_SETUP
  constexpr const char* identity="CSP-W2-%02x%02x%02x%02x%02x%02x";
  constexpr const char* advertisement="CSP-W2-%02x%02x%02x";
#else
  constexpr const char* identity="CSP-M1-%02x%02x%02x%02x%02x%02x";
  constexpr const char* advertisement="CSP-M1-%02x%02x%02x";
#endif
  snprintf(deviceId,sizeof deviceId,identity,
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  snprintf(deviceName,sizeof deviceName,advertisement,mac[3],mac[4],mac[5]);
  instance=protocomm_new();
  if(!instance){lastError.store(ESP_ERR_NO_MEM);return false;}
  esp_err_t result=protocomm_set_security(instance,"prov-session",&security(),&parameters);
  if(result==ESP_OK) result=protocomm_add_endpoint(instance,"csp-info",info,nullptr);
#if CSP_ENABLE_WIFI_SETUP
  if(result==ESP_OK) result=protocomm_add_endpoint(instance,"csp-wifi",wifi,nullptr);
#endif
  if(result==ESP_OK) result=protocomm_set_version(instance,"prov-version",VersionMetadata);
  if(result!=ESP_OK) {
    protocomm_delete(instance);instance=nullptr;lastError.store(result);return false;
  }
  protocomm_ble_config_t config{};
  snprintf(config.device_name,sizeof config.device_name,"%s",deviceName);
  memcpy(config.service_uuid,ServiceUuid,sizeof ServiceUuid);
  config.nu_lookup=endpoints;config.nu_lookup_count=sizeof endpoints/sizeof endpoints[0];
  // No bonding/NVS ownership changes; application Security2 remains mandatory.
  config.ble_bonding=false;config.ble_sm_sc=false;config.ble_link_encryption=false;
  config.keep_ble_on=false;config.ble_notify=false;
  uint32_t now=millis();
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    openedAt=now;connectedAt.store(0);lastActivity.store(now);
    session.store(NoSession);connected.store(false);authenticated.store(false);
    sequence.close();handshakeCalls.store(0);active.store(true);++stateRevision;
  }
  result=csp_ble_sdk_start(instance,&config);
  if(result!=ESP_OK) {
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      stopping.store(true);rejectLocked(result);++stateRevision;
    }
    // A failed startup may retain a late/running host. Its reviewed SDK wrapper
    // alone establishes quiescence; an INVALID_ARG return is insufficient.
    esp_err_t cleanup=csp_ble_sdk_stop(instance);
    bool idle=csp_ble_sdk_idle();
    if(idle) {
      protocomm_delete(instance);instance=nullptr;
      std::lock_guard<std::mutex> lock(stateMutex);
      active.store(false);sequence.close();session.store(NoSession);
      connected.store(false);authenticated.store(false);sessionOpening=false;
      ++sessionGeneration;++stateRevision;
    }else {
      std::lock_guard<std::mutex> lock(stateMutex);
      active.store(true);teardownFailed.store(true);
      if(cleanup!=ESP_OK)lastError.store(cleanup);
      ++stateRevision;
    }
    return false;
  }
  return true;
}
bool ble_management_stop() {
  if(!instance){active.store(false);return csp_ble_sdk_idle();}
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    // Never repeat destructive cleanup after uncertain SDK teardown.
    if(teardownFailed.load())return false;
    stopping.store(true);stopRequested.store(true);++stateRevision;
  }
  esp_err_t result=csp_ble_sdk_stop(instance);
  if(result!=ESP_OK || !csp_ble_sdk_idle()) {
    std::lock_guard<std::mutex> lock(stateMutex);
    teardownFailed.store(true);lastError.store(result!=ESP_OK?result:ESP_ERR_INVALID_STATE);
    ++stateRevision;return false;
  }
  // The wrapper has verified host/controller teardown before endpoint deletion.
  protocomm_delete(instance);instance=nullptr;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    sequence.close();session.store(NoSession);connected.store(false);
    authenticated.store(false);active.store(false);sessionOpening=false;
    stopping.store(false);++sessionGeneration;++stateRevision;
    // Keep terminal admission latched until the next physical-window begin.
  }
  return true;
}
void ble_management_poll() {
  struct Snapshot {
    bool connected,authenticated;
    uint32_t opened,connectedAt,lastActivity,window;
    uint64_t revision;
  } snapshot;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(!active.load() || teardownFailed.load()) return;
    snapshot={connected.load(),authenticated.load(),openedAt,connectedAt.load(),
      lastActivity.load(),windowGeneration,stateRevision};
  }
  // A callback may publish new activity while this clock read runs. Its changed
  // revision prevents applying the old terminal decision to that new state.
  uint32_t now=millis();
  bool windowTimeout=(!pro::setup_session::Guided||!snapshot.authenticated)&&pro::ble::elapsedAtLeast(now,snapshot.opened,WindowMs);
  bool sessionTimeout=(snapshot.connected && !snapshot.authenticated &&
      pro::ble::elapsedAtLeast(now,snapshot.connectedAt,HandshakeMs)) ||
    (snapshot.authenticated &&
      pro::ble::elapsedAtLeast(now,snapshot.lastActivity,IdleMs));
  bool lowHeap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<RuntimeFreeFloor ||
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<RuntimeBlockFloor;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(active.load() && !teardownFailed.load() && snapshot.window==windowGeneration) {
      if((windowTimeout&&(!pro::setup_session::Guided||snapshot.revision==stateRevision)) || (sessionTimeout && snapshot.revision==stateRevision))
        rejectLocked(ESP_ERR_TIMEOUT);
      if(lowHeap)rejectLocked(ESP_ERR_NO_MEM);
    }
  }
  if(stopRequested.load()) ble_management_stop();
}
bool ble_management_active(){return active.load();}
bool ble_management_authenticated(){return authenticated.load();}
int32_t ble_management_last_error(){return lastError.load();}
BleManagementStatus ble_management_status() {
  std::lock_guard<std::mutex> lock(stateMutex);
  return {active.load(),connected.load(),authenticated.load(),stopRequested.load(),
    stopping.load(),teardownFailed.load(),windowGeneration,openedAt,connectedAt.load(),
    lastActivity.load(),lastError.load()};
}
void ble_management_set_connection_status(bool value){wifiConnected.store(value);}
#if CSP_ENABLE_WIFI_SETUP
bool ble_management_authorizes(const WifiSetupSession& stamp){
  const uint32_t now=pro::setup_session::Guided?millis():0;
  std::lock_guard<std::mutex> lock(stateMutex);
  return admittedAtLocked(stamp.id,stamp.generation,now)&&authenticated.load();
}
#endif
#endif
