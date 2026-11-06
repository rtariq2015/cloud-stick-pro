#include <esp_wifi.h>
#include <cassert>
#include <cstring>
#include <iostream>

extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t*);
static unsigned calls=0,passed=0;
static esp_err_t result=ESP_OK;
static wifi_init_config_t captured{};
static const wifi_init_config_t* inputAddress=nullptr;
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config){
 assert(config&&config!=inputAddress);++calls;std::memcpy(&captured,config,sizeof captured);return result;
}
wifi_init_config_t configuration(){
 wifi_init_config_t c;std::memset(&c,0xa5,sizeof c);
 c.osi_funcs=&captured;
 for(unsigned i=0;i<24;++i)c.wpa_crypto_funcs.callbacks[i]=0x1000+i;
 c.static_rx_buf_num=4;c.dynamic_rx_buf_num=32;c.tx_buf_type=1;c.static_tx_buf_num=0;c.dynamic_tx_buf_num=32;
 c.rx_mgmt_buf_type=0;c.rx_mgmt_buf_num=5;c.cache_tx_buf_num=4;c.csi_enable=0;
 c.ampdu_rx_enable=1;c.ampdu_tx_enable=1;c.amsdu_tx_enable=0;c.nvs_enable=0;c.nano_enable=0;c.rx_ba_win=16;
 c.wifi_task_core_id=0;c.beacon_max_len=752;c.mgmt_sbuf_num=32;c.feature_caps=0x1020304050607080ULL;
 c.sta_disconnected_pm=true;c.espnow_max_encrypt_num=7;c.tx_hetb_queue_num=3;c.dump_hesigb_enable=false;c.magic=WIFI_INIT_CONFIG_MAGIC;
 return c;
}
void reset(){calls=0;result=ESP_OK;inputAddress=nullptr;}
void done(const char* name){++passed;std::cout<<"PASS "<<name<<'\n';}
void valid(wifi_init_config_t& config,int tx,int rx){
 const auto before=config;auto expected=config;expected.dynamic_tx_buf_num=tx;expected.dynamic_rx_buf_num=rx;
 inputAddress=&config;assert(__wrap_esp_wifi_init(&config)==ESP_OK);assert(calls==1);
 assert(std::memcmp(&config,&before,sizeof config)==0);
 assert(std::memcmp(&captured,&expected,sizeof captured)==0);
}
void invalid(wifi_init_config_t& config){
 const auto before=config;inputAddress=&config;
 assert(__wrap_esp_wifi_init(&config)==ESP_ERR_INVALID_ARG);assert(calls==0);
 assert(std::memcmp(&config,&before,sizeof config)==0);
}

int main(){
 reset();auto c=configuration();valid(c,8,16);done("stock Arduino32/32 is capped8/16; all other bytes and input preserved");
 reset();c=configuration();c.dynamic_tx_buf_num=4;c.dynamic_rx_buf_num=8;c.rx_ba_win=6;valid(c,4,8);done("compatible smaller finite counts are preserved");
 reset();c=configuration();c.dynamic_tx_buf_num=128;c.dynamic_rx_buf_num=128;valid(c,8,16);done("documented maximum counts receive finite caps");
 reset();c=configuration();c.dynamic_tx_buf_num=1;c.dynamic_rx_buf_num=4;c.rx_ba_win=4;valid(c,1,4);done("documented minimum TX and compatible RX are preserved");
 reset();c=configuration();c.ampdu_rx_enable=0;c.rx_ba_win=0;c.dynamic_rx_buf_num=4;valid(c,8,4);done("already disabled AMPDU is preserved without enabling or disabling features");
 reset();assert(__wrap_esp_wifi_init(nullptr)==ESP_ERR_INVALID_ARG&&calls==0);done("null configuration fails before real driver call");

 for(int value:{0,WIFI_INIT_CONFIG_MAGIC-1}){reset();c=configuration();c.magic=value;invalid(c);done("invalid magic fails closed");}
 for(int value:{0,2,-1}){reset();c=configuration();c.tx_buf_type=value;invalid(c);done("unexpected TX mode fails closed");}
 reset();c=configuration();c.static_tx_buf_num=8;invalid(c);done("mixed static/dynamic TX mode fails closed");
 for(int value:{0,1,26}){reset();c=configuration();c.static_rx_buf_num=value;invalid(c);done("unexpected static RX range fails closed");}
 for(int value:{-1,0,129}){reset();c=configuration();c.dynamic_tx_buf_num=value;invalid(c);done("invalid TX count fails closed");}
 for(int value:{-1,0,129}){reset();c=configuration();c.dynamic_rx_buf_num=value;invalid(c);done("invalid or unlimited RX count fails closed");}
 reset();c=configuration();c.static_rx_buf_num=20;invalid(c);done("RX cap never falls below static RX count");
 reset();c=configuration();c.dynamic_rx_buf_num=3;c.rx_ba_win=2;invalid(c);done("existing RX count below static RX fails closed");
 for(int value:{0,1,17,33}){reset();c=configuration();c.rx_ba_win=value;invalid(c);done("incompatible AMPDU window fails without changing it");}
 reset();c=configuration();c.ampdu_rx_enable=2;invalid(c);done("unexpected AMPDU mode fails closed");
 for(esp_err_t error:{ESP_FAIL,ESP_ERR_NO_MEM,ESP_ERR_INVALID_ARG}){
  reset();c=configuration();result=error;inputAddress=&c;
  assert(__wrap_esp_wifi_init(&c)==error&&calls==1);done("real driver failure is propagated exactly");
 }
 std::cout<<passed<<'/'<<passed<<" passed; SDK and Wi-Fi driver simulated\n";
}
