#include "config_loader.h"
#include <ArduinoJson.h>
#include <nvs.h>
#include <secure_json.h>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>

namespace {
unsigned passed=0;
std::string publicCA;
std::string fixture(const std::function<void(JsonDocument&)>& change={}){
  JsonDocument d;
  d["schema"]=1;d["mode"]="upload_only";
  d["server_url"]="https://synthetic.invalid/dav/";
  d["username"]="synthetic-cloud-user";d["password"]="synthetic-cloud-secret";
  d["ca_pem"]=publicCA;d["ntp_server"]="time.synthetic.invalid";
  // Deliberately invalid legacy Wi-Fi data must never affect v2 station ownership.
  d["ssid"]="";d["wifi_password"]="short";d["wifi_open"]=true;
  if(change)change(d);
  std::string out;serializeJson(d,out);return out;
}
void reset(std::string blob){
  assert(!config_fake::opened);
  config_fake::blob=std::move(blob);config_fake::useQueryOverride=config_fake::useReadOverride=false;
  config_fake::openResult=config_fake::queryResult=config_fake::readResult=ESP_OK;
  config_fake::opens=config_fake::closes=config_fake::queries=config_fake::reads=0;
}
void noMutation(){
  assert(!config_fake::opened&&config_fake::writes==0&&config_fake::erases==0);
  assert(config_fake::closes==(config_fake::openResult==ESP_OK?config_fake::opens:0));
}
void reject(){auto c=load_backup_config();assert(!c.is_valid&&c.ssid.empty()&&c.wifi_password.empty()&&!c.wifi_open);noMutation();}
void pass(const char* text){++passed;std::cout<<"PASS "<<text<<'\n';}
}
int main(int argc,char** argv){
  assert(argc==2);
  {
    using Allocator=pro::wifi_setup::ScrubJsonAllocator;
    Allocator allocator;
    auto* p=static_cast<uint8_t*>(allocator.allocate(Allocator::Limit));assert(p);
    std::memset(p,0xa5,Allocator::Limit);
    const auto half=Allocator::Limit/2;
    assert(allocator.reallocate(p,half)==p);
    for(size_t i=0;i<half;++i)assert(p[i]==0xa5);
    for(size_t i=half;i<Allocator::Limit;++i)assert(p[i]==0);
    // Shrinking in place did not physically free the retained capacity.
    assert(allocator.allocate(1)==nullptr);
    assert(allocator.reallocate(p,Allocator::Limit+1)==nullptr);
    for(size_t i=0;i<half;++i)assert(p[i]==0xa5); // Failed growth retains the old block.
    allocator.deallocate(p);
    p=static_cast<uint8_t*>(allocator.allocate(Allocator::Limit));assert(p);
    assert(allocator.reallocate(p,0)==nullptr);
    p=static_cast<uint8_t*>(allocator.allocate(Allocator::Limit));assert(p);allocator.deallocate(p);
  }
  pass("actual full-budget allocator shrink cannot fail, erases tail and retains physical accounting; failed growth preserves old block");
  std::ifstream f(argv[1]);assert(f.good());publicCA={std::istreambuf_iterator<char>(f),{}};
  assert(publicCA.size()>=64&&publicCA.size()<=4096);
  reset(fixture());auto c=load_backup_config();
  assert(c.is_valid&&c.ssid.empty()&&c.wifi_password.empty()&&!c.wifi_open);
  assert(c.server_url=="https://synthetic.invalid/dav/"&&c.username=="synthetic-cloud-user"&&c.password=="synthetic-cloud-secret");
  assert(c.watch_folder=="QA"&&c.backup_mode=="idle"&&c.idle_seconds==120);
  assert(c.sync_interval_s==60&&c.max_file_size_mb==64&&c.window_start==1&&c.window_end==5&&c.timezone=="EST5EDT,M3.2.0,M11.1.0");
  noMutation();pass("valid public CA/cloud configuration ignores invalid legacy Wi-Fi and preserves schedule defaults");

  reset(fixture([](JsonDocument& d){
    d["ssid"]=std::string(3000,'X');d["wifi_password"]="old-station-secret-marker";
    d["wifi_open"]=true;d["watch_folder"]="SYNTHETIC_QA";d["backup_mode"]="overnight";
    d["idle_seconds"]=330;d["window_start"]=22;d["window_end"]=6;d["timezone"]="UTC0";
  }));
  assert(config_fake::blob.size()<6144);c=load_backup_config();
  assert(c.is_valid&&c.ssid.empty()&&c.wifi_password.empty()&&!c.wifi_open);
  assert(c.watch_folder=="SYNTHETIC_QA"&&c.backup_mode=="overnight"&&c.idle_seconds==330&&c.window_start==22&&c.window_end==6&&c.timezone=="UTC0");
  noMutation();pass("oversized ignored SSID is filtered; configured overnight schedule is retained");

  reset(fixture());String reason;assert(!validate_config(c,reason));
  pass("ordinary legacy validator still requires valid Wi-Fi");

  for(const auto* key:{"server_url","username","password","ca_pem","ntp_server"}){
    reset(fixture([&](JsonDocument& d){d.remove(key);}));reject();
  }
  reset(fixture([](JsonDocument& d){d["server_url"]="http://synthetic.invalid/dav/";}));reject();
  reset(fixture([](JsonDocument& d){d["username"]="bad:user";}));reject();
  reset(fixture([](JsonDocument& d){d["password"]="bad\nsecret";}));reject();
  reset(fixture([](JsonDocument& d){d["ca_pem"]=std::string(256,'!');}));reject();
  pass("missing credentials, insecure URL, invalid credentials and invalid CA reject");

  for(const auto* key:{"sync_interval_s","max_file_size_mb","idle_seconds","window_start","window_end"}){
    reset(fixture([&](JsonDocument& d){d[key]=99999;}));reject();
  }
  reset(fixture([](JsonDocument& d){d["backup_mode"]="unsupported";}));reject();
  reset(fixture([](JsonDocument& d){d["watch_folder"]="../escape";}));reject();
  reset(fixture([](JsonDocument& d){d["timezone"]="unsupported";}));reject();
  pass("invalid schedule and backup boundaries reject");

  for(const auto* bad:{"{broken","[]","{}","null",""}){reset(bad);reject();}
  reset(fixture([](JsonDocument& d){d["schema"]=2;}));reject();
  reset(fixture([](JsonDocument& d){d["mode"]="two_way";}));reject();
  reset(std::string(6145,'x'));reject();assert(config_fake::reads==0);
  pass("corrupt schema/mode/JSON and oversized blob fail closed");

  reset(fixture());config_fake::openResult=ESP_FAIL;reject();assert(config_fake::reads==0);
  reset(fixture());config_fake::queryResult=ESP_FAIL;reject();assert(config_fake::reads==0);
  reset(fixture());config_fake::readResult=ESP_FAIL;reject();
  reset(fixture());config_fake::useReadOverride=true;config_fake::readOverride=0;reject();
  reset(fixture());config_fake::useReadOverride=true;config_fake::readOverride=config_fake::blob.size()+1;reject();
  pass("NVS open/query/read failures and impossible returned lengths fail closed without mutations");

  // A short successful read can itself contain complete valid JSON. Exact size
  // matching must reject replacement/shrink races rather than trusting parse.
  reset(fixture());config_fake::useQueryOverride=true;config_fake::queryOverride=config_fake::blob.size()+1;reject();
  pass("short successful complete-JSON read is rejected");
  std::cout<<passed<<" config adapter boundary groups passed; actual JSON/validation/x509; synthetic NVS; no writes/erases\n";
}
