#include "ble_platform_prepare.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <esp_bt.h>

const char* scenario="ok";
unsigned eventCalls=0,logCalls=0,mpiCalls=0,gcmCalls=0,shaCalls=0;
unsigned controllerInitCalls=0,controllerDeinitCalls=0;
esp_bt_controller_status_t controller=ESP_BT_CONTROLLER_STATUS_IDLE;
extern "C" esp_bt_controller_status_t esp_bt_controller_get_status(){return controller;}
extern "C" int esp_bt_controller_init(esp_bt_controller_config_t* c){
 assert(c&&c->synthetic_defaults==1234);++controllerInitCalls;
 if(!strcmp(scenario,"controller-init-fail"))return -1;
 if(!strcmp(scenario,"controller-partial-fail")){controller=ESP_BT_CONTROLLER_STATUS_INITED;return -1;}
 if(strcmp(scenario,"controller-init-no-state"))controller=ESP_BT_CONTROLLER_STATUS_INITED;
 return 0;
}
extern "C" int esp_bt_controller_deinit(){
 ++controllerDeinitCalls;assert(controller==ESP_BT_CONTROLLER_STATUS_INITED);
 if(!strcmp(scenario,"controller-deinit-fail"))return -1;
 if(strcmp(scenario,"controller-deinit-stuck"))controller=ESP_BT_CONTROLLER_STATUS_IDLE;
 return 0;
}
extern "C" int esp_event_loop_create_default(){
  ++eventCalls;
  if(!strcmp(scenario,"event-fail"))return -1;
  if(!strcmp(scenario,"existing-event"))return 259;
  return 0;
}
extern "C" void esp_log_level_set(const char* tag,int level){assert(tag&&level==2);++logCalls;}
#ifndef CSP_USE_REAL_CRYPTO
extern "C" int mbedtls_mpi_self_test(int verbose){assert(!verbose);++mpiCalls;return !strcmp(scenario,"mpi-fail")?1:0;}
extern "C" int mbedtls_gcm_self_test(int verbose){assert(!verbose);++gcmCalls;return !strcmp(scenario,"gcm-fail")?1:0;}
extern "C" int mbedtls_sha512_self_test(int verbose){assert(!verbose);++shaCalls;return !strcmp(scenario,"sha-fail")?1:0;}
#endif
int main(int argc,char** argv){
  assert(argc==2);scenario=argv[1];
  if(!strcmp(scenario,"controller-already-active"))controller=ESP_BT_CONTROLLER_STATUS_ENABLED;
  assert(!ble_platform_ready()&&ble_platform_error()==-2500);
  const bool success=!strcmp(scenario,"ok")||!strcmp(scenario,"existing-event");
  assert(ble_platform_prepare()==success);
  assert(ble_platform_ready()==success&&eventCalls==1&&logCalls==5);
  if(success)assert(ble_platform_error()==0);
  else{
    const int expected=!strcmp(scenario,"event-fail")?-2501:!strcmp(scenario,"mpi-fail")?-2502:
                       !strcmp(scenario,"gcm-fail")?-2503:!strcmp(scenario,"sha-fail")?-2504:
                       !strcmp(scenario,"controller-already-active")?-2505:
                       !strcmp(scenario,"controller-init-fail")||!strcmp(scenario,"controller-partial-fail")||
                       !strcmp(scenario,"controller-init-no-state")?-2506:-2507;
    assert(ble_platform_error()==expected);
  }
#ifndef CSP_USE_REAL_CRYPTO
  const unsigned a=mpiCalls,b=gcmCalls,c=shaCalls;
  assert(a==unsigned(strcmp(scenario,"event-fail")!=0));
  assert(b==unsigned(a&&strcmp(scenario,"mpi-fail")!=0));
  assert(c==unsigned(b&&strcmp(scenario,"gcm-fail")!=0));
#endif
  const unsigned init=controllerInitCalls,deinit=controllerDeinitCalls;
  if(success)assert(init==1&&deinit==1&&controller==ESP_BT_CONTROLLER_STATUS_IDLE);
  if(!strcmp(scenario,"controller-already-active"))assert(init==0&&deinit==0);
  if(!strcmp(scenario,"controller-init-fail")||!strcmp(scenario,"controller-partial-fail")||
     !strcmp(scenario,"controller-init-no-state"))assert(init==1&&deinit==0);
  if(!strcmp(scenario,"controller-deinit-fail")||!strcmp(scenario,"controller-deinit-stuck"))assert(init==1&&deinit==1);
  for(unsigned i=0;i<25;++i)assert(ble_platform_prepare()==success);
  assert(eventCalls==1&&logCalls==5);
  assert(controllerInitCalls==init&&controllerDeinitCalls==deinit);
#ifndef CSP_USE_REAL_CRYPTO
  assert(a==mpiCalls&&b==gcmCalls&&c==shaCalls);
#endif
  std::printf("PASS boot preparation %s; no retry or repeated initialization\n",scenario);
}
