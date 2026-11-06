// Actual BLE adapter, fake SDK crypto/lifecycle. Real Security2 authentication
// and AEAD tests live in security2_tests.py; no crypto claim is made here.
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "../milestone1_corrections/adapter_stubs/common.h"
#include "wifi_setup_runtime.h"
int simulatedStopCalls=0;bool simulatedStopOk=true;
std::mutex fixturePublication,fixtureMutex;std::condition_variable fixtureCondition;
std::atomic<bool> fixtureBarrier{false};bool sdkEntered=false,sdkRelease=false;unsigned acked=0;
std::function<void()> onClock,onSdk;
uint32_t millis(){if(onClock){auto hook=std::move(onClock);onClock=nullptr;hook();}return 100000;}
std::mutex& wifi_setup_owner_login_output_mutex(){return fixturePublication;}
bool wifi_setup_owner_login_info_allowed(){return !fixtureBarrier.load();}
bool wifi_setup_owner_login_output_allowed(const WifiSetupSession&,const uint8_t* bytes,size_t n,uint32_t& ticket){
  ticket=0;if(!fixtureBarrier.load())return true;
  if(n==3&&!std::memcmp(bytes,"ACK",3)){ticket=31;return true;}return false;
}
void wifi_setup_owner_login_ack_encrypted(const WifiSetupSession&,uint32_t ticket){assert(ticket==31);++acked;}
bool wifi_setup_owner_parameters(protocomm_security2_params_t& p){static char salt[16]{1},verifier[384]{1};p={salt,16,verifier,384};return true;}
esp_err_t wifi_setup_endpoint(const WifiSetupSession&,const pro::wifi_setup::WireRequest&,const uint8_t*,size_t,uint8_t**,ssize_t*){return ESP_ERR_INVALID_STATE;}
static int session_ok(void*,uint32_t){return ESP_OK;}
static int frame_ok(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*);
static int handshake_ok(void* h,const void*,uint32_t id,const uint8_t* b,ssize_t n,uint8_t** o,ssize_t* l,void*){return frame_ok(h,id,b,n,o,l);}
const protocomm_security_t protocomm_security2={2,1,nullptr,nullptr,session_ok,session_ok,handshake_ok,frame_ok,frame_ok};
#include "../../src/ble_management.cpp"
static int frame_ok(void*,uint32_t,const uint8_t* bytes,ssize_t size,uint8_t** out,ssize_t* length){
  if(onSdk)onSdk();*out=static_cast<uint8_t*>(std::malloc(size_t(size)));std::memcpy(*out,bytes,size_t(size));*length=size;return ESP_OK;
}
std::string infoReply(unsigned request){
  const auto text="{\"v\":1,\"id\":"+std::to_string(request)+",\"op\":\"info\"}";uint8_t* out=nullptr;ssize_t length=0;
  assert(info(7,reinterpret_cast<const uint8_t*>(text.data()),text.size(),&out,&length,nullptr)==ESP_OK);
  std::string reply(reinterpret_cast<char*>(out),size_t(length));std::free(out);return reply;
}
std::string encrypted(const std::string& plain){uint8_t* out=nullptr;ssize_t length=0;assert(encrypt(nullptr,7,reinterpret_cast<const uint8_t*>(plain.data()),plain.size(),&out,&length)==ESP_OK);std::string result(reinterpret_cast<char*>(out),size_t(length));std::free(out);return result;}
int main(int argc,char** argv){
  assert(argc==2);const std::string scenario=argv[1];assert(ble_management_begin()&&newSession(nullptr,7)==ESP_OK);
  const auto normal=infoReply(1);assert(authenticated.load()&&normal.find("device_id")!=std::string::npos);
  if(scenario=="lease"){
    onSdk=[] {std::unique_lock<std::mutex> lock(fixtureMutex);sdkEntered=true;fixtureCondition.notify_all();fixtureCondition.wait(lock,[]{return sdkRelease;});};
    std::string sent;std::thread encryption([&]{sent=encrypted(normal);});
    {std::unique_lock<std::mutex> lock(fixtureMutex);fixtureCondition.wait(lock,[]{return sdkEntered;});}
    assert(!fixturePublication.try_lock());
    std::atomic<bool> armed{false};std::thread arm([&]{std::lock_guard<std::mutex> lock(fixturePublication);fixtureBarrier=true;armed=true;});
    assert(!armed.load());{std::lock_guard<std::mutex> lock(fixtureMutex);sdkRelease=true;}fixtureCondition.notify_all();
    encryption.join();arm.join();onSdk=nullptr;assert(sent==normal&&armed.load());
  }else if(scenario=="late-info"){
    onClock=[] {std::lock_guard<std::mutex> lock(fixturePublication);fixtureBarrier=true;};
    const auto denied=infoReply(2);assert(denied.find("reconnect_required")!=std::string::npos&&denied.find("device_id")==std::string::npos);
  }else fixtureBarrier=true;
  assert(infoReply(scenario=="late-info"?3:2).find("reconnect_required")!=std::string::npos);
  const auto denied=encrypted(normal);assert(denied.find("reconnect_required")!=std::string::npos&&denied.find("device_id")==std::string::npos&&!stopRequested.load());
  assert(encrypted("ACK")=="ACK"&&acked==1&&!stopRequested.load());
  if(scenario=="malformed"){
    uint8_t* out=nullptr;ssize_t size=0;const char bad[]="{}";assert(info(7,reinterpret_cast<const uint8_t*>(bad),2,&out,&size,nullptr)!=ESP_OK);
    assert(stopRequested.load()&&!out&&size==0);
  }
  assert(ble_management_stop());std::printf("PASS actual BLE output barrier %s; SDK crypto/lifecycle mocked\n",scenario.c_str());
}
