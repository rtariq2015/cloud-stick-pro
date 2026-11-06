#include "config_loader.h"
#include "pro_core.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <nvs.h>
#include <memory>
#include <mbedtls/x509_crt.h>
#if CSP_ENABLE_BACKUP_SETUP
#include <secure_json.h>
#include <wifi_setup_model.h>
#endif
static bool text(const String& s,size_t max) {
  if(s.length()>max)return false;for(size_t i=0;i<s.length();++i)if(uint8_t(s[i])<32||uint8_t(s[i])==127)return false;return true;
}
static bool validate(const CloudConfig& c,String& error,bool requireWifi) {
  if(requireWifi&&!pro::validWiFi(std::string(c.ssid.c_str(),c.ssid.length()),std::string(c.wifi_password.c_str(),c.wifi_password.length()),c.wifi_open))error="Choose a Wi-Fi network; enter its 8-63 character password or select No password for an open network";
  else if(!pro::validURL(c.server_url.c_str()))error="Use an HTTPS WebDAV directory URL ending in /; no query or userinfo";
  else if(c.username.isEmpty()||!text(c.username,128)||c.username.indexOf(':')>=0||c.password.isEmpty()||!text(c.password,128))error="Invalid WebDAV credentials";
  else if(c.ntp_server.isEmpty()||!text(c.ntp_server,128)||c.ntp_server.indexOf('/')>=0||c.ntp_server.indexOf(' ')>=0)error="Enter an approved NTP hostname or IP";
  else if(c.sync_interval_s<30||c.sync_interval_s>3600||c.max_file_size_mb<1||c.max_file_size_mb>1024)error="Interval must be 30-3600 seconds; size limit 1-1024 MiB";
  else if(!pro::validPath(c.watch_folder.c_str()))error="Enter a relative backup folder, such as QA";
  else if(c.backup_mode!="idle"&&c.backup_mode!="overnight"&&c.backup_mode!="eject")error="Choose automatic, overnight or eject-only backup";
  else if(c.idle_seconds<30||c.idle_seconds>3600||c.window_start>23||c.window_end>23)error="Idle delay must be 30-3600 seconds; schedule hours must be 0-23";
  else if(c.timezone!="EST5EDT,M3.2.0,M11.1.0"&&c.timezone!="UTC0"&&c.timezone!="CST6CDT,M3.2.0,M11.1.0"&&c.timezone!="MST7MDT,M3.2.0,M11.1.0"&&c.timezone!="PST8PDT,M3.2.0,M11.1.0")error="Choose a supported time zone";
  else if(c.ca_pem.length()<64||c.ca_pem.length()>4096)error="Provide a trusted CA certificate PEM, at most 4096 bytes";
  else {
    mbedtls_x509_crt crt;mbedtls_x509_crt_init(&crt);int result=mbedtls_x509_crt_parse(&crt,reinterpret_cast<const unsigned char*>(c.ca_pem.c_str()),c.ca_pem.length()+1);mbedtls_x509_crt_free(&crt);
    if(result!=0)error="Invalid CA certificate PEM";else {error="";return true;}
  }
  return false;
}
bool validate_config(const CloudConfig& c,String& error){return validate(c,error,true);}
#if CSP_ENABLE_OWNER_CLOUD_SETUP
bool validate_backup_config(const CloudConfig& c,String& error){return validate(c,error,false);}
#endif
static String serialize(const CloudConfig& c) {
  JsonDocument doc;doc["watch_folder"]=c.watch_folder;doc["backup_mode"]=c.backup_mode;doc["idle_seconds"]=c.idle_seconds;doc["timezone"]=c.timezone;doc["window_start"]=c.window_start;doc["window_end"]=c.window_end;doc["schema"]=1;doc["mode"]="upload_only";doc["ssid"]=c.ssid;doc["wifi_open"]=c.wifi_open;doc["wifi_password"]=c.wifi_password;doc["server_url"]=c.server_url;doc["username"]=c.username;doc["password"]=c.password;doc["ca_pem"]=c.ca_pem;doc["ntp_server"]=c.ntp_server;doc["sync_interval_s"]=c.sync_interval_s;doc["max_file_size_mb"]=c.max_file_size_mb;String out;serializeJson(doc,out);return out;
}
CloudConfig load_config() {
  CloudConfig c;nvs_handle_t handle;if(nvs_open("csp",NVS_READONLY,&handle)!=ESP_OK)return c;
  size_t size=0;
  if(nvs_get_blob(handle,"config",nullptr,&size)!=ESP_OK||size==0||size>6144){nvs_close(handle);return c;}
  std::unique_ptr<char[]> buffer(new(std::nothrow) char[size+1]);
  if(!buffer||nvs_get_blob(handle,"config",buffer.get(),&size)!=ESP_OK){nvs_close(handle);return c;}
  buffer[size]='\0';nvs_close(handle);String raw=buffer.get();buffer.reset();
  if(raw.length()>6144)return c;
  JsonDocument doc;if(deserializeJson(doc,raw,DeserializationOption::NestingLimit(3))||doc["schema"]!=1||doc["mode"]!="upload_only")return c;
  c.ssid=doc["ssid"]|"";c.wifi_open=doc["wifi_open"]|false;c.wifi_password=doc["wifi_password"]|"";c.server_url=doc["server_url"]|"";c.username=doc["username"]|"";c.password=doc["password"]|"";c.ca_pem=doc["ca_pem"]|"";c.ntp_server=doc["ntp_server"]|"";c.sync_interval_s=doc["sync_interval_s"]|60;c.max_file_size_mb=doc["max_file_size_mb"]|64;
  c.watch_folder=doc["watch_folder"]|"QA";c.backup_mode=doc["backup_mode"]|"idle";c.idle_seconds=doc["idle_seconds"]|120;c.timezone=doc["timezone"]|"EST5EDT,M3.2.0,M11.1.0";c.window_start=doc["window_start"]|1;c.window_end=doc["window_end"]|5;
  String error;c.is_valid=validate_config(c,error);return c;
}
#if CSP_ENABLE_BACKUP_SETUP
static bool decode_backup_blob(const uint8_t* raw,size_t size,CloudConfig& c,
                               void(*release_input)(void*)=nullptr,void* context=nullptr) {
  c.is_valid=false;
  if(!raw||!size||size>6144)return false;
  pro::wifi_setup::ScrubJsonAllocator allocator,filterAllocator;
  JsonDocument doc(&allocator),filter(&filterAllocator);
  for(const char* key:{"schema","mode","server_url","username","password","ca_pem","ntp_server",
      "sync_interval_s","max_file_size_mb","watch_folder","backup_mode","idle_seconds","timezone","window_start","window_end"})filter[key]=true;
  const bool parsed=!deserializeJson(doc,raw,size,DeserializationOption::NestingLimit(3),DeserializationOption::Filter(filter));
  // Pinned ArduinoJson 7.4.3 copies this const input. The worker releases its
  // raw buffer at the original point, before String copies/X509 allocations.
  if(release_input)release_input(context);
  if(!parsed||doc.overflowed()||filter.overflowed()||doc["schema"]!=1||doc["mode"]!="upload_only")return false;
  c.server_url=doc["server_url"]|"";c.username=doc["username"]|"";c.password=doc["password"]|"";
  c.ca_pem=doc["ca_pem"]|"";c.ntp_server=doc["ntp_server"]|"";
  c.sync_interval_s=doc["sync_interval_s"]|60;c.max_file_size_mb=doc["max_file_size_mb"]|64;
  c.watch_folder=doc["watch_folder"]|"QA";c.backup_mode=doc["backup_mode"]|"idle";
  c.idle_seconds=doc["idle_seconds"]|120;c.timezone=doc["timezone"]|"EST5EDT,M3.2.0,M11.1.0";
  c.window_start=doc["window_start"]|1;c.window_end=doc["window_end"]|5;
  String error;c.is_valid=validate(c,error,false);return c.is_valid;
}
#if CSP_ENABLE_OWNER_CLOUD_SETUP
bool decode_backup_config_blob(const uint8_t* raw,size_t size,CloudConfig& c) {
  return decode_backup_blob(raw,size,c);
}
#endif
CloudConfig load_backup_config() {
  CloudConfig c;nvs_handle_t handle;
  if(nvs_open("csp",NVS_READONLY,&handle)!=ESP_OK)return c;
  size_t size=0;
  if(nvs_get_blob(handle,"config",nullptr,&size)!=ESP_OK||!size||size>6144){nvs_close(handle);return c;}
  std::unique_ptr<uint8_t[]> raw(new(std::nothrow) uint8_t[size]);
  if(!raw){nvs_close(handle);return c;}
  const size_t capacity=size;
  const bool read=nvs_get_blob(handle,"config",raw.get(),&size)==ESP_OK&&size==capacity;
  nvs_close(handle);
  struct Input {std::unique_ptr<uint8_t[]>* bytes;size_t capacity;} input{&raw,capacity};
  auto release=[](void* context){auto& value=*static_cast<Input*>(context);
    pro::wifi_setup::eraseSecretMemory(value.bytes->get(),value.capacity);value.bytes->reset();};
  if(read)decode_backup_blob(raw.get(),size,c,release,&input);
  if(raw)release(&input);
  return c;
}
#endif
bool save_config(const CloudConfig& c,String& error) {
  if(!validate_config(c,error))return false;
  String raw=serialize(c);if(raw.length()>6144){error="Configuration exceeds storage bound";return false;}
  Preferences prefs;if(!prefs.begin("csp",false)){error="Cannot open internal configuration";return false;}
  // NVS strings are limited to 4000 bytes; use one bounded atomic blob instead.
  bool ok=prefs.putBytes("config",raw.c_str(),raw.length())==raw.length();prefs.end();
  if(ok)ok=load_config().is_valid;
  if(!ok)error="Configuration write failed";return ok;
}
bool reset_config() {
  Preferences p;if(!p.begin("csp",false))return false;bool ok=p.remove("config");p.end();return ok;
}
