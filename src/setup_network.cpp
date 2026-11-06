#include "setup_network.h"
#include "pro_network.h"
#include <nvs.h>
namespace {
uint8_t selected=0;
constexpr char Namespace[]="csp-setup",Key[]="network";
bool read(nvs_handle_t handle,uint8_t& value){
  esp_err_t error=nvs_get_u8(handle,Key,&value);
  if(error==ESP_ERR_NVS_NOT_FOUND){value=0;return true;}
  return error==ESP_OK&&value<sizeof(pro::SetupAddresses)/sizeof(pro::SetupAddresses[0]);
}
}
bool setup_network_load(){
  nvs_handle_t handle;if(nvs_open(Namespace,NVS_READONLY,&handle)!=ESP_OK)return false;
  uint8_t value=0;bool ok=read(handle,value);nvs_close(handle);
  if(ok)selected=value;return ok;
}
bool setup_network_save(uint32_t address){
  uint8_t index=0;for(;index<sizeof(pro::SetupAddresses)/sizeof(pro::SetupAddresses[0]);++index)if(pro::SetupAddresses[index]==address)break;
  if(index>=sizeof(pro::SetupAddresses)/sizeof(pro::SetupAddresses[0]))return false;
  nvs_handle_t handle;if(nvs_open(Namespace,NVS_READWRITE,&handle)!=ESP_OK)return false;
  bool ok=nvs_set_u8(handle,Key,index)==ESP_OK&&nvs_commit(handle)==ESP_OK;nvs_close(handle);
  if(!ok||nvs_open(Namespace,NVS_READONLY,&handle)!=ESP_OK)return false;
  uint8_t value=255;ok=read(handle,value)&&value==index;nvs_close(handle);
  if(ok)selected=index;return ok;
}
IPAddress setup_network_ip(){uint32_t address=pro::SetupAddresses[selected];return IPAddress(address>>24,(address>>16)&255,(address>>8)&255,address&255);}
bool setup_network_client(const IPAddress& ip){
  IPAddress ap=setup_network_ip();return ip.type()==IPv4&&ip[0]==ap[0]&&ip[1]==ap[1]&&ip[2]==ap[2]&&ip[3]>=2&&ip[3]<=254;
}
