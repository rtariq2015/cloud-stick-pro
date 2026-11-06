#include "wifi_setup_nvs.h"
#if CSP_ENABLE_WIFI_SETUP
#include <secure_json.h>
#include <nvs.h>
#include <esp_mac.h>
#include <esp_err.h>
#include <cstring>
#include <memory>
using namespace pro::wifi_setup;
namespace {
WifiOwnerInstallStep owner_install_step=WifiOwnerInstallStep::Never;
bool allowedKey(const char* key){return key&&(!std::strcmp(key,"a")||!std::strcmp(key,"b")||!std::strcmp(key,"active"));}
Read readBlob(const char* space,const char* key,uint8_t* out,size_t cap,size_t& size){
  size=0;nvs_handle_t h;auto rc=nvs_open(space,NVS_READONLY,&h);
  if(rc==ESP_ERR_NVS_NOT_FOUND)return Read::Missing;if(rc!=ESP_OK)return Read::Error;
  size_t n=0;rc=nvs_get_blob(h,key,nullptr,&n);
  if(rc==ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return Read::Missing;}
  if(rc!=ESP_OK||!n||n>cap){nvs_close(h);return Read::Error;}
  rc=nvs_get_blob(h,key,out,&n);nvs_close(h);if(rc!=ESP_OK||n>cap)return Read::Error;
  size=n;return Read::Found;
}
bool writeBlob(const char* space,const char* key,const uint8_t* p,size_t n){
  nvs_handle_t h;if(nvs_open(space,NVS_READWRITE,&h)!=ESP_OK)return false;
  const bool ok=nvs_set_blob(h,key,p,n)==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);return ok;
}
}
Read WifiNvsStore::read(const char* key,uint8_t* out,size_t cap,size_t& n){
  n=0;if(!allowedKey(key))return Read::Error;return readBlob("csp-wifi2",key,out,cap,n);
}
bool WifiNvsStore::write(const char* key,const uint8_t* p,size_t n){
  if(!allowedKey(key)||!p||n!=(std::strcmp(key,"active")==0?SelectionSize:NetworkRecordSize))return false;
  return writeBlob("csp-wifi2",key,p,n);
}
WifiOwnerLoad wifi_owner_load(OwnerRecord& out){
  eraseSecretMemory(&out,sizeof out);
  std::array<uint8_t,OwnerRecordSize> bytes{};size_t n=0;uint8_t mac[6]{};
  const auto r=readBlob("csp-owner","owner",bytes.data(),bytes.size(),n);
  if(r==Read::Missing)return WifiOwnerLoad::Missing;
  const bool ok=r==Read::Found&&esp_read_mac(mac,ESP_MAC_BT)==ESP_OK&&decodeOwner(bytes.data(),n,mac,out);
  eraseSecretMemory(bytes.data(),bytes.size());return ok?WifiOwnerLoad::Ready:WifiOwnerLoad::Fault;
}
bool wifi_owner_first_install(const uint8_t* p,size_t n){
  OwnerRecord old,checked;
  owner_install_step=WifiOwnerInstallStep::ExistingOwner;
  if(wifi_owner_load(old)!=WifiOwnerLoad::Missing)return false;
  owner_install_step=WifiOwnerInstallStep::Identity;
  uint8_t mac[6]{};if(esp_read_mac(mac,ESP_MAC_BT)!=ESP_OK)return false;
  owner_install_step=WifiOwnerInstallStep::Record;
  if(!decodeOwner(p,n,mac,checked))return false;
  eraseSecretMemory(&checked,sizeof checked);
  // Both a successful SDK commit and exact readback are mandatory. A failed
  // commit followed by a readable key is still uncertain, not verified durable.
  owner_install_step=WifiOwnerInstallStep::Write;
  const bool written=writeBlob("csp-owner","owner",p,n);
  if(written)owner_install_step=WifiOwnerInstallStep::Readback;
  std::array<uint8_t,OwnerRecordSize> read{};size_t size=0;
  const bool ok=written&&readBlob("csp-owner","owner",read.data(),read.size(),size)==Read::Found&&
    size==n&&std::memcmp(p,read.data(),n)==0;
  eraseSecretMemory(read.data(),read.size());
  if(ok)owner_install_step=WifiOwnerInstallStep::Verified;
  return ok;
}
WifiOwnerInstallStep wifi_owner_install_step(){return owner_install_step;}
Stored wifi_import_legacy_network(StoredNetworks& out){
  // Import only the Wi-Fi fields. The legacy cloud/CA/schedule blob remains
  // byte-for-byte untouched; no cloud client is constructed in this image.
  std::unique_ptr<uint8_t[]> raw(new(std::nothrow) uint8_t[6144]);if(!raw)return Stored::IO;
  size_t n=0;const auto result=readBlob("csp","config",raw.get(),6144,n);
  if(result!=Read::Found){eraseSecretMemory(raw.get(),6144);return result==Read::Missing?Stored::Missing:Stored::IO;}
  ScrubJsonAllocator allocator,filter_allocator;JsonDocument doc(&allocator),filter(&filter_allocator);
  filter["schema"]=true;filter["mode"]=true;filter["ssid"]=true;filter["wifi_password"]=true;filter["wifi_open"]=true;
  const auto error=deserializeJson(doc,raw.get(),n,DeserializationOption::NestingLimit(3),DeserializationOption::Filter(filter));
  eraseSecretMemory(raw.get(),6144);raw.reset();
  if(error||doc.overflowed()||filter.overflowed()||doc["schema"]!=1||doc["mode"]!="upload_only"||
    !doc["ssid"].is<JsonString>()||!doc["wifi_password"].is<JsonString>()||!doc["wifi_open"].is<bool>())return Stored::Corrupt;
  auto ssid=doc["ssid"].as<JsonString>(),pass=doc["wifi_password"].as<JsonString>();
  const bool open=doc["wifi_open"].as<bool>();
  if(!validSSID({ssid.c_str(),ssid.size()})||(open?pass.size()!=0:!validPassword({pass.c_str(),pass.size()})))return Stored::Corrupt;
  out={};out.count=1;out.selected=1;out.next_id=2;auto& p=out.profiles[0];p.id=1;p.security=open?Security::Open:Security::Wpa2Personal;
  std::memcpy(p.ssid.data(),ssid.c_str(),ssid.size());std::memcpy(p.secret.data(),pass.c_str(),pass.size());return validStored(out)?Stored::Valid:Stored::Corrupt;
}
// Arduino normally attempts whole-NVS erase after selected init errors. Refuse
// that behavior in the review-only setup target; never erase production keys.
extern "C" esp_err_t __wrap_nvs_flash_erase(){return ESP_ERR_NOT_SUPPORTED;}
extern "C" esp_err_t __wrap_nvs_flash_erase_partition(const char*){return ESP_ERR_NOT_SUPPORTED;}
#endif
