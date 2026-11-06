// Actual M2 adapter/runtime/NVS/model + actual pinned SDK Security 2 crypto.
// Radio, clock, NVS hardware and USB interfaces are synthetic desktop backends.
#include "stubs/common.h"
#include "wifi_setup_radio.h"
#include "ble_sdk_lifecycle.h"
#include "ble_platform_prepare.h"
#include "usb_storage.h"
#include "lcd.h"
#include <fstream>
#include <iterator>
#include <cassert>
uint32_t testNow=100000;
unsigned nextHandle=0,nvsWrites=0,usbInitializations=0,usbPolls=0;
bool testOwnerDown=false,testStorageReady=false,testAuthorized=true,failWrites=false,loseAck=false;
bool loseActiveAck=false;
int simulatedStopCalls=0;bool simulatedStopOk=true;
std::map<std::string,std::vector<uint8_t>> testNvs;
std::map<unsigned,std::string> testHandles;
std::map<unsigned,std::string> testLastKey;
FakeWiFi WiFi;FakeSerial USBSerial;
bool ble_platform_prepare(){return true;}
bool ble_platform_ready(){return true;}
int32_t ble_platform_error(){return 0;}
extern "C" int csp_ble_sdk_start(protocomm_t*,const protocomm_ble_config_t*){return ESP_OK;}
extern "C" int csp_ble_sdk_stop(protocomm_t*){return simulatedStopOk?ESP_OK:ESP_FAIL;}
extern "C" bool csp_ble_sdk_idle(){return simulatedStopOk;}
extern "C" bool csp_ble_sdk_ready(){return true;}
extern "C" uint32_t csp_ble_sdk_host_stack_min_bytes(){return 2048;}
void init_usb_storage(bool){++usbInitializations;}
void poll_storage(){++usbPolls;}
pro::Owner storage_owner(){return pro::Owner::Host;}
bool storage_ready(){return testStorageReady;}
bool storage_media_detected(){return testStorageReady;}
void lcd_init(){}
void lcd_show_loading(){}
void lcd_show_text(const char*){}
WifiSetupRadioSnapshot radio;
bool failRadio=false;unsigned radioTicks=0;
bool wifi_setup_radio_init(){return true;}
bool wifi_setup_radio_scan(){radio.state=WifiSetupRadioState::LocalConnected;radio.error=pro::wifi_setup::Error::LocalOnly;radio.has_scan=true;radio.count=2;
  std::strcpy(radio.rows[0].ssid,"CSP Test WiFi");radio.rows[0].rssi=-55;radio.rows[0].supported=true;
  std::strcpy(radio.rows[1].ssid,"CSP Test Open");radio.rows[1].rssi=-65;radio.rows[1].supported=true;radio.rows[1].open=true;return true;}
bool wifi_setup_radio_connect(const pro::wifi_setup::CheckProfile& p){radio.profile=p.id;radio.state=WifiSetupRadioState::Connecting;radio.error=pro::wifi_setup::Error::None;radioTicks=0;return true;}
bool wifi_setup_radio_disconnect(){radio.state=WifiSetupRadioState::Idle;return true;}
void wifi_setup_radio_poll(){if(radio.state==WifiSetupRadioState::Connecting&&++radioTicks>1){
  radio.state=failRadio?WifiSetupRadioState::Failed:WifiSetupRadioState::LocalConnected;
  radio.error=failRadio?pro::wifi_setup::Error::WrongPassword:pro::wifi_setup::Error::LocalOnly;}}
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot(){return radio;}
#include "../../src/ble_management.cpp"
int fromHex(const char* input,uint8_t* out,size_t cap){size_t n=std::strlen(input);if(n%2||n/2>cap)return -1;
  for(size_t i=0;i<n;i+=2){unsigned value;if(std::sscanf(input+i,"%2x",&value)!=1)return -1;out[i/2]=value;}return n/2;}
int main(int argc,char** argv){
  assert(argc==2);std::ifstream file(argv[1],std::ios::binary);assert(file);
  testNvs["csp-owner/owner"]={std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
  const std::string legacy=R"({"schema":1,"mode":"upload_only","ssid":"CSP Previous Network","wifi_password":"public-fixture-old","wifi_open":false,"password":"SYNTHETIC_CLOUD_SENTINEL_NEVER_COPY","ca_pem":"SYNTHETIC_CA_SENTINEL"})";
  testNvs["csp/config"]={legacy.begin(),legacy.end()};const auto original=testNvs["csp/config"];
  wifi_setup_runtime_setup();assert(usbInitializations==1&&nvsWrites==0);
  void* handle=nullptr;assert(ble_management_begin());assert(protocomm_security2.init(&handle)==ESP_OK);assert(newSession(handle,1)==ESP_OK);
  char line[4096];uint8_t input[1024];
  while(std::fgets(line,sizeof line,stdin)){
    line[std::strcspn(line,"\r\n")]=0;char* payload=std::strchr(line,' ');if(payload)*payload++=0;else payload=const_cast<char*>("");
    const int size=fromHex(payload,input,sizeof input);int error=ESP_ERR_INVALID_ARG;uint8_t* out=nullptr;ssize_t length=0;
    if(!std::strcmp(line,"session")&&size>0)error=secureHandshake(handle,&parameters,1,input,size,&out,&length,nullptr);
    else if((!std::strcmp(line,"info")||!std::strcmp(line,"wifi")||!std::strcmp(line,"wifi-no-poll"))&&size>0){
      uint8_t* plain=nullptr;ssize_t plain_size=0;error=decrypt(handle,1,input,size,&plain,&plain_size);
      if(error==ESP_OK){uint8_t* answer=nullptr;ssize_t answer_size=0;
        error=!std::strcmp(line,"info")?info(1,plain,plain_size,&answer,&answer_size,nullptr):wifi(1,plain,plain_size,&answer,&answer_size,nullptr);
        if(error==ESP_OK)error=encrypt(handle,1,answer,answer_size,&out,&length);
        std::free(answer);
      }std::free(plain);
      // Run the actual owner loop between simulated GATT requests, not inside
      // the callback. Hardware workers remain the deterministic stubs above.
      if(std::strcmp(line,"wifi-no-poll"))wifi_setup_runtime_loop();
    }else if(!std::strcmp(line,"version")){length=sizeof VersionMetadata-1;out=static_cast<uint8_t*>(std::malloc(length));std::memcpy(out,VersionMetadata,length);error=ESP_OK;}
    else if(!std::strcmp(line,"tick")){wifi_setup_runtime_loop();error=ESP_OK;}
    else if(!std::strcmp(line,"fail-write")){failWrites=true;error=ESP_OK;}
    else if(!std::strcmp(line,"lost-ack")){loseAck=true;error=ESP_OK;}
    else if(!std::strcmp(line,"lost-active-ack")){loseActiveAck=true;error=ESP_OK;}
    else if(!std::strcmp(line,"clear-fault")){failWrites=loseAck=loseActiveAck=failRadio=false;error=ESP_OK;}
    else if(!std::strcmp(line,"radio-fail")){failRadio=true;error=ESP_OK;}
    else if(!std::strcmp(line,"reject")){error=reject(ESP_ERR_INVALID_ARG);}
    else if(!std::strcmp(line,"close")){error=closeSession(handle,1);wifi_setup_runtime_loop();}
    else if(!std::strcmp(line,"window")){
      assert(ble_management_stop());assert(protocomm_security2.cleanup(handle)==ESP_OK);handle=nullptr;
      assert(ble_management_begin());assert(protocomm_security2.init(&handle)==ESP_OK);error=newSession(handle,1);
    }else if(!std::strcmp(line,"quit"))break;
    std::printf("{\"error\":%d,\"data\":\"",error);
    if(error==ESP_OK&&out&&length>=0&&length<=1024)for(ssize_t i=0;i<length;++i)std::printf("%02x",out[i]);
    std::printf("\",\"writes\":%u,\"legacy_unchanged\":%s,\"usb_polls\":%u,\"terminal\":%s}\n",nvsWrites,testNvs["csp/config"]==original?"true":"false",usbPolls,stopRequested.load()?"true":"false");
    std::fflush(stdout);std::free(out);assert(testNvs["csp/config"]==original);
    for(const auto& item:testNvs)if(item.first.rfind("csp-wifi2/",0)==0){const std::string blob(item.second.begin(),item.second.end());assert(blob.find("SYNTHETIC_CLOUD_SENTINEL")==std::string::npos);}
  }
  assert(ble_management_stop());protocomm_security2.cleanup(handle);assert(testHandles.empty());return 0;
}
