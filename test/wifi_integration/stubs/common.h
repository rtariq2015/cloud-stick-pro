#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <ArduinoJson.h>
#include <sys/types.h>
using esp_err_t=int;
using protocomm_security_handle_t=void*;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=0x102,
  ESP_ERR_INVALID_STATE=0x103, ESP_ERR_INVALID_SIZE=0x104,
  ESP_ERR_NO_MEM=0x105, ESP_ERR_TIMEOUT=0x106;
constexpr int ESP_LOG_WARN=2, ESP_MAC_BT=1, MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
struct protocomm_t { int unused; };
struct protocomm_security_t {
 int ver; uint8_t patch_ver;
 esp_err_t (*init)(void**);esp_err_t (*cleanup)(void*);
 esp_err_t (*new_transport_session)(void*,uint32_t);
 esp_err_t (*close_transport_session)(void*,uint32_t);
 esp_err_t (*security_req_handler)(void*,const void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*);
 esp_err_t (*encrypt)(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*);
 esp_err_t (*decrypt)(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*);
};
struct protocomm_security2_params_t { const char* salt;uint16_t salt_len;const char* verifier;uint16_t verifier_len; };
struct protocomm_ble_name_uuid_t { const char* name;uint16_t uuid; };
struct protocomm_ble_config_t {
 char device_name[32];uint8_t service_uuid[16];
 protocomm_ble_name_uuid_t* nu_lookup;size_t nu_lookup_count;
 bool ble_bonding,ble_sm_sc,ble_link_encryption,keep_ble_on,ble_notify;
};
extern "C" { extern const protocomm_security_t protocomm_security2; }
extern uint32_t testNow;
inline uint32_t millis(){return testNow;}
extern int simulatedStopCalls;
extern bool simulatedStopOk;
inline void esp_log_level_set(const char*,int){}
inline int esp_event_loop_create_default(){return ESP_OK;}
inline int esp_read_mac(uint8_t* out,int){const uint8_t mac[6]={0,0x11,0x22,0x33,0x44,0x55};std::memcpy(out,mac,6);return ESP_OK;}
inline size_t heap_caps_get_free_size(int){return 500000;}
inline size_t testLargestBlock=250000;
inline size_t heap_caps_get_largest_free_block(int){return testLargestBlock;}
inline size_t heap_caps_get_minimum_free_size(int){return 500000;}
inline unsigned uxTaskGetStackHighWaterMark(void*){return 4096;}
inline protocomm_t* protocomm_new(){return new protocomm_t{};}
inline void protocomm_delete(protocomm_t* pc){delete pc;}
inline int protocomm_set_security(protocomm_t*,const char*,const protocomm_security_t*,const void*){return ESP_OK;}
using handler_t=esp_err_t(*)(uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*);
inline int protocomm_add_endpoint(protocomm_t*,const char*,handler_t,void*){return ESP_OK;}
inline int protocomm_set_version(protocomm_t*,const char*,const char*){return ESP_OK;}
inline int protocomm_ble_start(protocomm_t*,const protocomm_ble_config_t*){return ESP_OK;}
// Return failure so the harness never pretends the blocked callback has drained.
inline int protocomm_ble_stop(protocomm_t*){++simulatedStopCalls;return simulatedStopOk?ESP_OK:ESP_FAIL;}

using String=std::string;
constexpr int LOW=0,HIGH=1,INPUT_PULLUP=2,WIFI_STA=1,WL_CONNECTED=3;
constexpr int ESP_ERR_NOT_SUPPORTED=0x108,ESP_ERR_NVS_NOT_FOUND=0x1102;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
using nvs_handle_t=unsigned;
extern std::map<std::string,std::vector<uint8_t>> testNvs;
extern std::map<unsigned,std::string> testHandles;
extern unsigned nextHandle,nvsWrites,usbInitializations,usbPolls;
extern bool testOwnerDown,testStorageReady,testAuthorized,failWrites,loseAck;
extern bool loseActiveAck;
extern std::map<unsigned,std::string> testLastKey;
inline int nvs_open(const char* space,int mode,nvs_handle_t* out){
  if(mode==NVS_READONLY){bool found=false;for(const auto& p:testNvs)if(p.first.rfind(std::string(space)+"/",0)==0)found=true;if(!found)return ESP_ERR_NVS_NOT_FOUND;}
  *out=++nextHandle;testHandles[*out]=space;return ESP_OK;
}
inline int nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* length){
  const auto it=testNvs.find(testHandles[h]+"/"+key);if(it==testNvs.end())return ESP_ERR_NVS_NOT_FOUND;
  if(!out){*length=it->second.size();return ESP_OK;}
  if(*length<it->second.size())return ESP_ERR_INVALID_SIZE;
  *length=it->second.size();std::memcpy(out,it->second.data(),*length);return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t size){
  ++nvsWrites;if(failWrites)return ESP_ERR_NO_MEM;
  testLastKey[h]=key;
  const auto* p=static_cast<const uint8_t*>(data);testNvs[testHandles[h]+"/"+key]={p,p+size};return ESP_OK;
}
inline int nvs_commit(nvs_handle_t h){return loseAck||(loseActiveAck&&testLastKey[h]=="active")?ESP_FAIL:ESP_OK;}
inline void nvs_close(nvs_handle_t h){testHandles.erase(h);testLastKey.erase(h);}
inline void delay(uint32_t n){testNow+=n;}
inline void pinMode(int,int){}
inline int digitalRead(int){return testOwnerDown?LOW:HIGH;}
struct FakeWiFi{int status(){return WL_CONNECTED;}};extern FakeWiFi WiFi;
struct FakeSerial {
  std::deque<char> input;std::vector<std::string> lines;
  void setTxTimeoutMs(unsigned){}
  int available()const{return input.size();}
  int read(){auto c=input.front();input.pop_front();return c;}
  void println(const char* text){lines.emplace_back(text);}
  void send(const std::string& text){for(char c:text)input.push_back(c);input.push_back('\n');}
  void printf(const char* format,...){char text[2048];va_list args;va_start(args,format);std::vsnprintf(text,sizeof text,format,args);va_end(args);lines.emplace_back(text);}
};extern FakeSerial USBSerial;
