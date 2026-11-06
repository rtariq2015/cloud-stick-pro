#include "owner_cloud_feature.h"
#if CSP_ENABLE_OWNER_CLOUD_SETUP
#include "owner_cloud_store.h"
#include "config_loader.h"
#include <secure_json.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>

namespace pro::owner_cloud {
namespace {
using pro::wifi_setup::Read;
constexpr char Root[]="https://app.koofr.net/dav/Koofr/";
// Before ArduinoJson allocation, bound the entire legacy shape and reject key
// aliases/duplicates. Unknown fields remain in the cloned document. Escaped or
// non-ASCII keys are refused rather than silently rewritten. Values may use
// ordinary JSON escapes; the actual parser still validates their contents.
struct Shape {
  struct Frame {std::array<std::string_view,64> keys{};size_t count=0;};
  std::unique_ptr<Frame[]> frames{new(std::nothrow) Frame[3]};
  const char* bytes;size_t length,at=0,tokens=0,strings=0;
  Shape(const uint8_t* input,size_t size):bytes(reinterpret_cast<const char*>(input)),length(size){}
  void space(){while(at<length&&(bytes[at]==' '||bytes[at]=='\t'||bytes[at]=='\r'||bytes[at]=='\n'))++at;}
  bool take(char c){space();if(at==length||bytes[at]!=c)return false;++at;return true;}
  bool string(std::string_view* key=nullptr){
    if(!take('"')||++strings>128)return false;const size_t start=at;
    while(at<length){
      const unsigned char c=bytes[at++];
      if(c=='"'){if(key){if(at-start-1>64)return false;*key={bytes+start,at-start-1};}return true;}
      if(c<32||(key&&c>=127))return false;
      if(c>=128){
        unsigned following=0;uint32_t value=0;
        if(c>=0xc2&&c<=0xdf){following=1;value=c&31;}
        else if(c>=0xe0&&c<=0xef){following=2;value=c&15;}
        else if(c>=0xf0&&c<=0xf4){following=3;value=c&7;}
        else return false;
        if(at+following>length)return false;
        for(unsigned i=0;i<following;++i){const auto next=uint8_t(bytes[at++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if((following==2&&value<0x800)||(following==3&&value<0x10000)||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
      }
      if(c=='\\'){
        if(key||at==length)return false;const char escape=bytes[at++];
        if(escape=='u'){
          unsigned value=0;
          for(unsigned i=0;i<4;++i){if(at==length)return false;const char h=bytes[at++];
            unsigned digit=h>='0'&&h<='9'?unsigned(h-'0'):h>='a'&&h<='f'?unsigned(h-'a'+10):h>='A'&&h<='F'?unsigned(h-'A'+10):16;
            if(digit==16)return false;value=(value<<4)|digit;}
          if(value>=0xdc00&&value<=0xdfff)return false;
          if(value>=0xd800&&value<=0xdbff){
            if(at+6>length||bytes[at++]!='\\'||bytes[at++]!='u')return false;unsigned low=0;
            for(unsigned i=0;i<4;++i){const char h=bytes[at++];unsigned d=h>='0'&&h<='9'?unsigned(h-'0'):h>='a'&&h<='f'?unsigned(h-'a'+10):h>='A'&&h<='F'?unsigned(h-'A'+10):16;if(d==16)return false;low=(low<<4)|d;}
            if(low<0xdc00||low>0xdfff)return false;
          }
        }else if(escape!='"'&&escape!='\\'&&escape!='/'&&escape!='b'&&escape!='f'&&escape!='n'&&escape!='r'&&escape!='t')return false;
      }
    }return false;
  }
  bool literal(const char* text){const size_t n=std::strlen(text);if(at+n>length||std::memcmp(bytes+at,text,n))return false;at+=n;return true;}
  bool number(){
    const size_t begin=at;if(at<length&&bytes[at]=='-')++at;if(at==length)return false;
    if(bytes[at]=='0')++at;else{if(bytes[at]<'1'||bytes[at]>'9')return false;while(at<length&&bytes[at]>='0'&&bytes[at]<='9')++at;}
    if(at<length&&bytes[at]=='.'){++at;const auto first=at;while(at<length&&bytes[at]>='0'&&bytes[at]<='9')++at;if(first==at)return false;}
    if(at<length&&(bytes[at]=='e'||bytes[at]=='E')){++at;if(at<length&&(bytes[at]=='+'||bytes[at]=='-'))++at;const auto first=at;while(at<length&&bytes[at]>='0'&&bytes[at]<='9')++at;if(first==at)return false;}
    const size_t size=at-begin;if(size>32)return false;
    char number[33]{};std::memcpy(number,bytes+begin,size);char* end=nullptr;errno=0;
    const double value=std::strtod(number,&end);if(errno==ERANGE||!std::isfinite(value)||end!=number+size)return false;
    if(!std::strchr(number,'.')&&!std::strchr(number,'e')&&!std::strchr(number,'E')){
      const bool negative=number[0]=='-';uint64_t magnitude=0;const uint64_t maximum=negative?uint64_t(INT64_MAX)+1:UINT64_MAX;
      for(size_t i=negative?1:0;i<size;++i){const auto digit=unsigned(number[i]-'0');if(magnitude>(maximum-digit)/10)return false;magnitude=magnitude*10+digit;}
    }return true;
  }
  bool value(unsigned depth){
    space();if(at==length||++tokens>128)return false;
    if(bytes[at]=='{')return object(depth+1);
    if(bytes[at]=='['){if(depth>=3)return false;++at;space();if(at<length&&bytes[at]==']'){++at;return true;}for(;;){if(!value(depth+1))return false;space();if(at==length)return false;if(bytes[at]==']'){++at;return true;}if(bytes[at++]!=',')return false;}}
    if(bytes[at]=='"')return string();
    if(bytes[at]=='t')return literal("true");if(bytes[at]=='f')return literal("false");if(bytes[at]=='n')return literal("null");return number();
  }
  bool object(unsigned depth){
    if(depth>3||!take('{'))return false;auto& frame=frames[depth-1];frame.count=0;
    space();if(at<length&&bytes[at]=='}'){++at;return true;}
    for(;;){
      std::string_view key;if(!string(&key)||frame.count==frame.keys.size())return false;
      for(size_t i=0;i<frame.count;++i)if(frame.keys[i]==key)return false;frame.keys[frame.count++]=key;
      if(!take(':')||!value(depth))return false;space();if(at==length)return false;
      if(bytes[at]=='}'){++at;return true;}if(bytes[at++]!=',')return false;
    }
  }
  bool valid(){if(!frames||!length||length>BlobLimit||std::memchr(bytes,0,length))return false;if(!object(1))return false;space();return at==length;}
};
enum class ShapeResult:uint8_t {Valid,Invalid,NoMemory};
ShapeResult shape(const uint8_t* bytes,size_t length){Shape value(bytes,length);if(!value.frames)return ShapeResult::NoMemory;return value.valid()?ShapeResult::Valid:ShapeResult::Invalid;}
bool textType(JsonObjectConst doc,const char* name){
  if(doc[name].isUnbound())return true;auto value=doc[name];if(!value.is<JsonString>())return false;
  const auto text=value.as<JsonString>();return !std::memchr(text.c_str(),0,text.size());
}
bool fields(JsonObjectConst doc,uint64_t& revision){
  if(!doc["schema"].is<unsigned>()||doc["schema"].as<unsigned>()!=1||!doc["mode"].is<JsonString>()||doc["mode"]!="upload_only")return false;
  revision=1;if(!doc["cloud_rev"].isUnbound()){
    if(!doc["cloud_rev"].is<uint64_t>()||!(revision=doc["cloud_rev"].as<uint64_t>()))return false;
  }
  for(const char* key:{"server_url","username","password","ca_pem","ntp_server","watch_folder","backup_mode","timezone"})if(!textType(doc,key))return false;
  for(const char* key:{"sync_interval_s","max_file_size_mb","idle_seconds","window_start","window_end"})
    if(!doc[key].isUnbound()&&!doc[key].is<uint32_t>())return false;
  return true;
}
void wipe(String& text){if(text.length())pro::wifi_setup::eraseSecretMemory(const_cast<char*>(text.c_str()),text.length());}
struct PrivateConfig {
  CloudConfig value;
  ~PrivateConfig(){for(String* text:{&value.ssid,&value.wifi_password,&value.server_url,&value.username,&value.password,&value.ca_pem,&value.ntp_server,&value.watch_folder,&value.backup_mode,&value.timezone})wipe(*text);}
};
void decode(JsonObjectConst doc,CloudConfig& out){
  out.server_url=doc["server_url"]|"";out.username=doc["username"]|"";out.password=doc["password"]|"";out.ca_pem=doc["ca_pem"]|"";out.ntp_server=doc["ntp_server"]|"";
  out.sync_interval_s=doc["sync_interval_s"]|60;out.max_file_size_mb=doc["max_file_size_mb"]|64;
  out.watch_folder=doc["watch_folder"]|"QA";out.backup_mode=doc["backup_mode"]|"idle";out.idle_seconds=doc["idle_seconds"]|120;
  out.timezone=doc["timezone"]|"EST5EDT,M3.2.0,M11.1.0";out.window_start=doc["window_start"]|1;out.window_end=doc["window_end"]|5;
}
bool configValid(JsonObjectConst doc,PublicSettings* public_settings=nullptr){
  PrivateConfig config;decode(doc,config.value);String error;
  const bool valid=validate_backup_config(config.value,error);
  if(valid&&public_settings){
    const auto& c=config.value;
    if(c.ntp_server.length()>=public_settings->ntp_server.size()||c.timezone.length()>=public_settings->timezone.size())return false;
    std::memcpy(public_settings->ntp_server.data(),c.ntp_server.c_str(),c.ntp_server.length());
    std::memcpy(public_settings->timezone.data(),c.timezone.c_str(),c.timezone.length());
  }return valid;
}
bool folderFrom(JsonVariantConst url,std::string_view& folder){
  if(!url.is<JsonString>())return false;const auto raw=url.as<JsonString>();std::string_view value(raw.c_str(),raw.size());
  constexpr size_t prefix=sizeof Root-1;if(value.size()<=prefix||value.substr(0,prefix)!=Root||value.back()!='/')return false;
  folder=value.substr(prefix,value.size()-prefix-1);return validFolder(folder);
}
template<size_t N>bool copy(JsonVariantConst value,std::array<char,N>& out,bool username){
  if(!value.is<JsonString>())return false;const auto text=value.as<JsonString>();
  if(!validCredential({text.c_str(),text.size()},username)||text.size()>=N)return false;
  std::memcpy(out.data(),text.c_str(),text.size());return true;
}
Read read(pro::wifi_setup::BlobStore& store,Blob& bytes,size_t& size){
  size=0;if(!bytes.allocate(BlobLimit))return Read::Error;
  const auto state=store.read("config",bytes.data(),BlobLimit,size);
  if(state==Read::Found&&(!size||size>BlobLimit))return Read::Error;return state;
}
bool same(const Record& baseline,Read state,const Blob& current,size_t length){
  return baseline.missing?state==Read::Missing:state==Read::Found&&length==baseline.raw.size()&&
    !std::memcmp(current.data(),baseline.raw.data(),length);
}
bool legacyWifi(JsonDocument& doc,const pro::wifi_setup::StoredProfile* selected){
  if(!doc["ssid"].isUnbound()&&!doc["wifi_password"].isUnbound()&&!doc["wifi_open"].isUnbound())return true;
  if(!selected||!selected->id||!selected->enabled)return false;
  const auto ssid=std::find(selected->ssid.begin(),selected->ssid.end(),'\0');
  const auto password=std::find(selected->secret.begin(),selected->secret.end(),'\0');
  if(ssid==selected->ssid.end()||password==selected->secret.end())return false;
  const std::string_view name(selected->ssid.data(),size_t(ssid-selected->ssid.begin()));
  const std::string_view secret(selected->secret.data(),size_t(password-selected->secret.begin()));
  const bool open=selected->security==pro::wifi_setup::Security::Open;
  if(!pro::wifi_setup::validSSID(name)||(open?!secret.empty():selected->security!=pro::wifi_setup::Security::Wpa2Personal||!pro::wifi_setup::validPassword(secret)))return false;
  if(doc["ssid"].isUnbound())doc["ssid"]=selected->ssid.data();
  if(doc["wifi_password"].isUnbound())doc["wifi_password"]=selected->secret.data();
  if(doc["wifi_open"].isUnbound())doc["wifi_open"]=open;return true;
}
}

Loaded Store::load(Record& out){
  out.raw.reset();out.initial=Values{};out.info=Info{};out.missing=true;
  size_t length=0;const auto state=read(blobs_,out.raw,length);
  if(state==Read::Missing){out.raw.reset();return Loaded::Missing;}
  if(state!=Read::Found){out.raw.reset();return Loaded::Unavailable;}
  if(!out.raw.trim(length))return Loaded::Corrupt;
  const auto syntax=shape(out.raw.data(),length);if(syntax==ShapeResult::NoMemory)return Loaded::Unavailable;if(syntax!=ShapeResult::Valid)return Loaded::Corrupt;
  pro::wifi_setup::ScrubJsonAllocator allocator;JsonDocument doc(&allocator);
  const auto parsed=deserializeJson(doc,out.raw.data(),length,DeserializationOption::NestingLimit(3));
  if(parsed==DeserializationError::NoMemory||doc.overflowed())return Loaded::Unavailable;
  if(parsed||!doc.is<JsonObject>()||!fields(doc.as<JsonObjectConst>(),out.info.revision))return Loaded::Corrupt;
  out.missing=false;out.info.configured=configValid(doc.as<JsonObjectConst>());
  std::string_view folder;
  if(folderFrom(doc["server_url"],folder)){
    out.info.provider=Provider::Koofr;
    out.info.has_folder=true;std::memcpy(out.initial.folder.data(),folder.data(),folder.size());
    out.info.has_username=copy(doc["username"],out.initial.username,true);
    out.info.has_password=copy(doc["password"],out.initial.password,false);
  }else if(doc["server_url"].is<JsonString>()&&doc["server_url"].as<JsonString>().size())out.info.provider=Provider::Other;
  return Loaded::Valid;
}
Saved Store::publish(const Record& baseline,const Values& values,const pro::wifi_setup::StoredProfile* selected,
                     uint64_t expected,pro::wifi_setup::StillAuthorized authorized,void* context,PublicSettings& applied){
  applied=PublicSettings{};
  if(!authorized||!authorized(context))return Saved::Cancelled;
  if(!expected||expected!=baseline.info.revision)return Saved::Conflict;
  if(expected==std::numeric_limits<uint64_t>::max())return Saved::Exhausted;
  if(!complete(values)||!ca_||std::strlen(ca_)<64||std::strlen(ca_)>4096)return Saved::Invalid;
  Blob current;size_t length=0;auto state=read(blobs_,current,length);
  if(state==Read::Error)return Saved::Unavailable;if(!same(baseline,state,current,length))return Saved::Conflict;
  // The next fresh full-value CAS needs another read anyway. Release this
  // first comparison buffer before allocating editing/worker parser scratch.
  current.reset();
  Blob image;PublicSettings next;
  {
    pro::wifi_setup::ScrubJsonAllocator allocator;JsonDocument doc(&allocator);
    if(!baseline.missing){
      const auto syntax=shape(baseline.raw.data(),baseline.raw.size());if(syntax==ShapeResult::NoMemory)return Saved::Unavailable;if(syntax!=ShapeResult::Valid)return Saved::Invalid;
      const auto parsed=deserializeJson(doc,baseline.raw.data(),baseline.raw.size(),DeserializationOption::NestingLimit(3));
      if(parsed==DeserializationError::NoMemory||doc.overflowed())return Saved::Unavailable;
      if(parsed||!doc.is<JsonObject>())return Saved::Invalid;
      uint64_t revision=0;if(!fields(doc.as<JsonObjectConst>(),revision))return Saved::Invalid;if(revision!=expected)return Saved::Conflict;
    }else{
      if(baseline.raw.size())return Saved::Invalid;
      doc["schema"]=1;doc["mode"]="upload_only";doc["ntp_server"]="pool.ntp.org";doc["watch_folder"]="QA";
      doc["backup_mode"]="idle";doc["idle_seconds"]=120;doc["sync_interval_s"]=60;doc["max_file_size_mb"]=64;
      doc["timezone"]="EST5EDT,M3.2.0,M11.1.0";doc["window_start"]=1;doc["window_end"]=5;
    }
    if(!legacyWifi(doc,selected))return Saved::Invalid;
    char url[160]{};const int n=std::snprintf(url,sizeof url,"%s%s/",Root,values.folder.data());if(n<=0||size_t(n)>=sizeof url)return Saved::Invalid;
    doc["server_url"]=url;doc["username"]=values.username.data();doc["password"]=values.password.data();doc["ca_pem"]=ca_;
    doc["cloud_rev"]=expected+1;
    if(doc.overflowed()||!configValid(doc.as<JsonObjectConst>(),&next))return Saved::Invalid;
    const size_t bytes=measureJson(doc);if(!bytes||bytes>BlobLimit||!image.allocate(bytes))return Saved::Unavailable;
    if(serializeJson(doc,reinterpret_cast<char*>(image.data()),image.size())!=bytes)return Saved::Invalid;
    next.revision=expected+1;
  }
  // Use the worker's actual filtered decoder and unchanged parser budget. A
  // candidate unreadable by backups must fail before the persistent write.
  {
    PrivateConfig worker;
    if(!decode_backup_config_blob(image.data(),image.size(),worker.value))return Saved::Unavailable;
    if(worker.value.username!=values.username.data()||worker.value.password!=values.password.data())return Saved::Invalid;
  }
  // The single owner task serializes config writers. Recheck the entire private
  // value immediately before the linearization point; revisions alone cannot
  // detect a legacy writer that changed another field without bumping cloud_rev.
  state=read(blobs_,current,length);
  if(state==Read::Error)return Saved::Unavailable;if(!same(baseline,state,current,length))return Saved::Conflict;
  if(!authorized(context))return Saved::Cancelled;
  if(!blobs_.write("config",image.data(),image.size()))return Saved::Unknown;
  length=0;state=blobs_.read("config",current.data(),BlobLimit,length);
  if(state!=Read::Found||length!=image.size()||std::memcmp(current.data(),image.data(),length))return Saved::Unknown;
  applied=next;return Saved::Committed;
}
}
#endif
