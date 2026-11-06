#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <string>
#include <vector>
#include <ArduinoJson.h>
using String=std::string;
using esp_err_t=int;
struct esp_partition_t {int unused;};
constexpr int ESP_OK=0,ESP_ERR_NOT_SUPPORTED=0x106;
constexpr int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,LOW=0,HIGH=1,INPUT_PULLUP=2;
constexpr int ESP_RST_SW=3;
extern uint32_t testNow;
extern bool testButtonDown;
extern unsigned usbInitializations,usbPolls,forbiddenCalls,resets,diagnosticPolls;
extern size_t testFreeHeap,testLargestHeap;
inline uint32_t millis(){return testNow;}
inline void delay(uint32_t ms){testNow+=ms;}
inline void pinMode(int,int){}
inline int digitalRead(int){return testButtonDown?LOW:HIGH;}
inline int esp_reset_reason(){return 1;}
inline size_t heap_caps_get_free_size(int){return testFreeHeap;}
inline size_t heap_caps_get_minimum_free_size(int){return testFreeHeap;}
inline size_t heap_caps_get_largest_free_block(int){return testLargestHeap;}
inline unsigned uxTaskGetStackHighWaterMark(void*){return 1234;}
struct FakeESP {void restart(){++resets;}};extern FakeESP ESP;
struct FakeSerial {
 std::deque<char> input;std::vector<std::string> lines;
 operator bool()const{return true;}
 void begin(int){}
 void setTxTimeoutMs(int){}
 int available()const{return input.size();}
 int read(){char c=input.front();input.pop_front();return c;}
 void println(const std::string& value){lines.push_back(value);}
 void printf(const char*,...){}
 void send(const std::string& value){for(char c:value)input.push_back(c);input.push_back(10);}
};extern FakeSerial USBSerial;
struct CloudConfig {bool is_valid=true;};
inline CloudConfig load_config(){++forbiddenCalls;return {};}
inline void reset_config(){++forbiddenCalls;}
inline void configTzTime(const char*,const char*){++forbiddenCalls;}
struct FakeWiFi {
 void persistent(bool){++forbiddenCalls;}void mode(int){++forbiddenCalls;}
 void begin(const char*,const char*){++forbiddenCalls;}void reconnect(){++forbiddenCalls;}
};extern FakeWiFi WiFi;
inline void start_backup(const CloudConfig&){++forbiddenCalls;}
inline bool request_connection_check(bool=false){++forbiddenCalls;return false;}
inline void save_config(const CloudConfig&){++forbiddenCalls;}
inline void receipt_init(){++forbiddenCalls;}
inline void diagnostics_persist(){++forbiddenCalls;}
inline void cloud_check(){++forbiddenCalls;}
inline void init_usb_storage(bool ejected=false){if(ejected)++forbiddenCalls;++usbInitializations;}
inline void poll_storage(){++usbPolls;}
inline int storage_owner(){return 0;}
inline uint64_t storage_generation(){return 9;}
inline void lcd_init(){}
inline void lcd_show_loading(){}
inline void lcd_show_text(const char*){}
inline void dashboard_poll(){++forbiddenCalls;}
inline bool portal_active(){++forbiddenCalls;return false;}
inline bool portal_start(){++forbiddenCalls;return false;}
inline void portal_poll(){++forbiddenCalls;}
bool csp_ble_sdk_ready();
bool csp_ble_sdk_idle();
uint32_t csp_ble_sdk_host_stack_min_bytes();
