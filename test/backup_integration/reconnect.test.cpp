#include "pro_wifi_reconnect.h"
#include <cassert>
#include <iostream>
int main(){
  pro::wifi_setup::StoredNetworks networks;
  networks.count=3;networks.selected=1;
  for(unsigned i=0;i<3;++i){networks.profiles[i].id=i+1;networks.profiles[i].priority=i;}
  pro::WifiReconnect retry;retry.revision(networks.revision);
  assert(retry.choose(networks,0)==1);
  retry.failed(1,100);assert(retry.choose(networks,101)==3);
  retry.failed(3,101);assert(retry.choose(networks,102)==2);
  retry.failed(2,102);assert(retry.choose(networks,103)==0);
  assert(retry.choose(networks,10100)==1);
  retry.failed(1,10100);assert(!retry.available(1,30099)&&retry.available(1,30100));
  retry.connected(1);assert(retry.available(1,10101));
  networks.profiles[0].enabled=false;assert(retry.choose(networks,10101)!=1);
  retry.revision(2);assert(retry.choose(networks,10101)==3);
  networks.profiles[2].priority=networks.profiles[1].priority;
  assert(retry.choose(networks,10101)==2); // Stable tie prevents oscillation.
  networks.profiles[0].enabled=true;retry.revision(3);
  retry.failed(1,0xfffffff0u);
  assert(!retry.available(1,0xfffffff1u)&&!retry.available(1,0x2000u));
  assert(retry.available(1,uint32_t(0xfffffff0u+10000u)));
  for(unsigned i=0;i<20;++i)retry.failed(1,i*1000000u);
  assert(!retry.available(1,19000000u+299999)&&retry.available(1,19000000u+300000));
  const auto baseline=networks;
  for(unsigned i=0;i<10000;++i)(void)retry.choose(networks,i*31);
  assert(networks.revision==baseline.revision&&networks.selected==baseline.selected);
  for(size_t i=0;i<networks.count;++i){
    const auto& a=networks.profiles[i];const auto& b=baseline.profiles[i];
    assert(a.id==b.id&&a.priority==b.priority&&a.enabled==b.enabled&&a.ssid==b.ssid&&a.secret==b.secret);
  }
  networks.count=999;assert(retry.choose(networks,0)==0);
  std::cout<<"Reconnect: saved-profile selection, capped cooldown, disabled profiles, stable ties, rollover, no mutation PASS\n";
}
