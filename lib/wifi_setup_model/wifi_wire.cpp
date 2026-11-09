#include "wifi_wire.h"
#include "secure_json.h"
#include <cstring>
#include <string_view>
namespace pro::wifi_setup {
namespace {
// Flat objects only, literal ASCII keys, no duplicates or nested allocations.
// ArduinoJson handles standard value escapes after this strict bounded prepass.
bool flat(const uint8_t* p,size_t n) {
  size_t at=0,count=0;std::array<std::string_view,16> keys{};
  auto ws=[&](){while(at<n&&(p[at]==' '||p[at]=='\n'||p[at]=='\r'||p[at]=='\t'))++at;};
  ws();if(at==n||p[at++]!='{')return false;
  for(;;){
    ws();if(at==n)return false;
    if(p[at]=='}'){++at;break;}
    if(count==keys.size()||p[at++]!='"')return false;
    const size_t start=at;
    while(at<n&&((p[at]>='a'&&p[at]<='z')||p[at]=='_'))++at;
    if(at==start||at-start>24||at==n||p[at++]!='"')return false;
    std::string_view key(reinterpret_cast<const char*>(p+start),at-start-1);
    for(size_t i=0;i<count;++i)if(keys[i]==key)return false;
    keys[count++]=key;ws();if(at==n||p[at++]!=':')return false;ws();if(at==n)return false;
    if(p[at]=='"'){
      ++at;bool ended=false;
      while(at<n){auto c=p[at++];if(c=='"'){ended=true;break;}if(c<32)return false;
        if(c=='\\'){if(at==n)return false;auto escaped=p[at++];if(escaped=='u'){
          for(unsigned k=0;k<4;++k){if(at==n)return false;auto h=p[at++];if(!((h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F')))return false;}
        }else if(escaped!='"'&&escaped!='\\'&&escaped!='/'&&escaped!='b'&&escaped!='f'&&escaped!='n'&&escaped!='r'&&escaped!='t')return false;}
      }if(!ended)return false;
    }else if(p[at]>='0'&&p[at]<='9'){
      const size_t number=at;while(at<n&&p[at]>='0'&&p[at]<='9')++at;
      if(at-number>10||(at-number>1&&p[number]=='0'))return false;
    }else if(at+4<=n&&std::memcmp(p+at,"true",4)==0)at+=4;
    else if(at+5<=n&&std::memcmp(p+at,"false",5)==0)at+=5;
    else return false;
    ws();if(at==n)return false;
    if(p[at]=='}'){++at;break;}
    if(p[at++]!=',')return false;ws();if(at==n||p[at]=='}')return false;
  }
  ws();return at==n;
}
bool number64(JsonVariantConst v,uint64_t& out){
  if(!v.is<JsonString>())return false;auto s=v.as<JsonString>();
  if(!s.size()||s.size()>20||(s.size()>1&&s.c_str()[0]=='0'))return false;
  uint64_t n=0;for(size_t i=0;i<s.size();++i){auto c=s.c_str()[i];if(c<'0'||c>'9'||n>(UINT64_MAX-uint64_t(c-'0'))/10)return false;n=n*10+uint64_t(c-'0');}out=n;return n>0;
}
bool fields(JsonObjectConst obj,const char* wanted){
  // wanted is delimiter-terminated so field-name prefixes cannot match.
  for(auto pair:obj){bool found=false;const char* at=wanted;const auto name=pair.key();
    while(*at){const char* end=std::strchr(at,',');if(!end)return false;
      if(size_t(end-at)==name.size()&&std::memcmp(at,name.c_str(),name.size())==0){found=true;break;}at=end+1;}
    if(!found)return false;
  }return true;
}
template<size_t N>bool text(JsonVariantConst value,std::array<char,N>& out){
  if(!value.is<JsonString>())return false;auto s=value.as<JsonString>();if(s.size()>=N)return false;
  for(size_t i=0;i<s.size();++i)if(s.c_str()[i]==0)return false;
  out.fill(0);std::memcpy(out.data(),s.c_str(),s.size());return true;
}
}
bool parseWire(const uint8_t* p,size_t n,WireRequest& r){
  if(!p||!n||n>WifiWireLimit||!flat(p,n))return false;
  ScrubJsonAllocator allocator;JsonDocument doc(&allocator);
  if(deserializeJson(doc,p,n,DeserializationOption::NestingLimit(1))||doc.overflowed()||!doc.is<JsonObject>()||
     !doc["v"].is<unsigned>()||doc["v"].as<unsigned>()!=2||!doc["id"].is<uint32_t>()||
     !(r.id=doc["id"].as<uint32_t>())||r.id>0x7fffffff||!doc["op"].is<const char*>())return false;
  auto obj=doc.as<JsonObjectConst>();auto operation=doc["op"].as<JsonString>();
  std::string_view op(operation.c_str(),operation.size());
#if defined(CSP_ENABLE_BLE_WIFI_WAKE) && CSP_ENABLE_BLE_WIFI_WAKE
  if(op=="wake_pair"){
    r.op=Op::WakePair;
    if(obj.size()!=4||!fields(obj,"v,id,op,key,")||!doc["key"].is<JsonString>())return false;
    const auto key=doc["key"].as<JsonString>();if(key.size()!=130)return false;
    auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<r.wake_key.size();++i){const int h=digit(key.c_str()[i*2]),l=digit(key.c_str()[i*2+1]);if(h<0||l<0)return false;r.wake_key[i]=uint8_t(h*16+l);}
    return r.wake_key[0]==4;
  }
#endif
#if defined(CSP_ENABLE_OWNER_LOGIN) && CSP_ENABLE_OWNER_LOGIN
  if(op=="login_begin"){r.op=Op::LoginBegin;return obj.size()==3&&fields(obj,"v,id,op,");}
  if(op=="login_abort"){
    r.op=Op::LoginAbort;return obj.size()==4&&fields(obj,"v,id,op,transaction,")&&number64(doc["transaction"],r.transaction);
  }
  if(op=="login_commit"){
    r.op=Op::LoginCommit;
    if(obj.size()!=7||!fields(obj,"v,id,op,transaction,revision,edit,accept,")||
       !number64(doc["transaction"],r.transaction)||!number64(doc["revision"],r.revision)||
       !doc["edit"].is<uint32_t>()||!doc["accept"].is<bool>())return false;
    r.edit=doc["edit"].as<uint32_t>();r.confirm=doc["accept"].as<bool>();return true;
  }
  if(op=="login_stage"){
    r.op=Op::LoginStage;
    if(obj.size()!=7||!fields(obj,"v,id,op,transaction,edit,offset,data,")||
       !number64(doc["transaction"],r.transaction)||!doc["edit"].is<uint32_t>()||
       !doc["offset"].is<uint32_t>()||!doc["data"].is<JsonString>())return false;
    const auto hex=doc["data"].as<JsonString>();const auto bytes=hex.size()/2;
    r.cursor=doc["offset"].as<uint32_t>();r.edit=doc["edit"].as<uint32_t>();
    if(!hex.size()||hex.size()%2||bytes>r.login_bytes.size()||r.cursor>OwnerRecordSize||bytes>OwnerRecordSize-r.cursor)return false;
    auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(size_t i=0;i<bytes;++i){const int high=digit(hex.c_str()[2*i]),low=digit(hex.c_str()[2*i+1]);if(high<0||low<0)return false;r.login_bytes[i]=uint8_t(high*16+low);}
    r.login_size=uint16_t(bytes);return true;
  }
#endif
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
  if(op=="cloud_status"){r.op=Op::CloudStatus;return obj.size()==3&&fields(obj,"v,id,op,");}
  if(op=="cloud_begin"){
    r.op=Op::CloudBegin;return obj.size()==4&&fields(obj,"v,id,op,revision,")&&number64(doc["revision"],r.revision);
  }
  if(op=="cloud_abort"){
    r.op=Op::CloudAbort;return obj.size()==4&&fields(obj,"v,id,op,transaction,")&&number64(doc["transaction"],r.transaction);
  }
  if(op=="cloud_stage"){
    r.op=Op::CloudStage;
    if(obj.size()!=7||!fields(obj,"v,id,op,transaction,edit,field,value,")||!number64(doc["transaction"],r.transaction)||
       !doc["edit"].is<uint32_t>()||!doc["field"].is<JsonString>()||!text(doc["value"],r.cloud_value))return false;
    const auto name=doc["field"].as<JsonString>();const std::string_view field(name.c_str(),name.size());
    if(field=="username")r.cloud_field=pro::owner_cloud::Field::Username;
    else if(field=="app_password")r.cloud_field=pro::owner_cloud::Field::AppPassword;
    else if(field=="folder")r.cloud_field=pro::owner_cloud::Field::Folder;
    else return false;
    r.edit=doc["edit"].as<uint32_t>();return true;
  }
  if(op=="cloud_commit"){
    r.op=Op::CloudCommit;
    if(obj.size()!=7||!fields(obj,"v,id,op,transaction,revision,edit,accept_unverified,")||
       !number64(doc["transaction"],r.transaction)||!number64(doc["revision"],r.revision)||
       !doc["edit"].is<uint32_t>()||!doc["accept_unverified"].is<bool>())return false;
    r.edit=doc["edit"].as<uint32_t>();r.accept_unverified=doc["accept_unverified"].as<bool>();return true;
  }
#endif
#if defined(CSP_ENABLE_FILE_HISTORY) && CSP_ENABLE_FILE_HISTORY && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  if(op=="history_status"){r.op=Op::HistoryStatus;return obj.size()==3&&fields(obj,"v,id,op,");}
  if(op=="history_row"){
    r.op=Op::HistoryRow;
    if(obj.size()!=6||!fields(obj,"v,id,op,g,i,o,")||!doc["g"].is<uint32_t>()||
       !doc["i"].is<uint32_t>()||!doc["o"].is<uint32_t>())return false;
    // Existing fields are operation-specific: no production request layout or
    // persistent settings structure changes for this read-only view.
    r.edit=doc["g"].as<uint32_t>();r.profile=doc["i"].as<uint32_t>();r.cursor=doc["o"].as<uint32_t>();
    return r.edit>0&&r.profile<8&&r.cursor<=240;
  }
#endif
#if defined(CSP_ENABLE_EVENT_LOG) && CSP_ENABLE_EVENT_LOG && defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  if(op=="event_log"){
    r.op=Op::EventLog;
    if(obj.size()!=4||!fields(obj,"v,id,op,cursor,")||!doc["cursor"].is<uint32_t>())return false;
    r.cursor=doc["cursor"].as<uint32_t>();return true;
  }
#endif
#if defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
  if(op=="portal_status"){r.op=Op::PortalStatus;return obj.size()==3&&fields(obj,"v,id,op,");}
  if(op=="portal_cancel"){
    r.op=Op::PortalCancel;return obj.size()==4&&fields(obj,"v,id,op,grant,")&&number64(doc["grant"],r.grant);
  }
  if(op=="portal_begin"||op=="portal_handoff"){
    const bool handing=op=="portal_handoff";
    r.op=handing?Op::PortalHandoff:Op::PortalBegin;
    if(obj.size()!=size_t(handing?6:5)||!fields(obj,handing?"v,id,op,profile,revision,grant,":"v,id,op,profile,revision,")||
       !doc["profile"].is<uint32_t>()||!(r.profile=doc["profile"].as<uint32_t>())||!number64(doc["revision"],r.revision))return false;
    return !handing||number64(doc["grant"],r.grant);
  }
#endif
  if(op=="list"){
    r.op=Op::List;if(obj.size()==3)return fields(obj,"v,id,op,");
    if(obj.size()!=4||!fields(obj,"v,id,op,cursor,")||!doc["cursor"].is<uint32_t>())return false;
    r.cursor=doc["cursor"].as<uint32_t>();return r.cursor<=8;
  }
  if(op=="scan"||op=="status"){
    r.op=op=="scan"?Op::Scan:Op::Status;return obj.size()==3&&fields(obj,"v,id,op,");
  }
#if defined(CSP_ENABLE_BACKUP_SETUP) && CSP_ENABLE_BACKUP_SETUP
  if(op=="backup_status"){
    r.op=Op::BackupStatus;return obj.size()==3&&fields(obj,"v,id,op,");
  }
#endif
  if(op=="scan_page"){
    r.op=Op::ScanPage;if(obj.size()!=4||!fields(obj,"v,id,op,cursor,")||!doc["cursor"].is<uint32_t>())return false;
    r.cursor=doc["cursor"].as<uint32_t>();return r.cursor<=20;
  }
  if(op=="result"){
    r.op=Op::Result;if(obj.size()!=4||!fields(obj,"v,id,op,job,")||!doc["job"].is<uint32_t>())return false;
    r.job=doc["job"].as<uint32_t>();return r.job!=0;
  }
  if(op=="begin"){r.op=Op::Begin;return obj.size()==4&&fields(obj,"v,id,op,revision,")&&number64(doc["revision"],r.revision);}
  if(!number64(doc["transaction"],r.transaction))return false;
  if(op=="abort"){r.op=Op::Abort;return obj.size()==4&&fields(obj,"v,id,op,transaction,");}
  if(op=="commit"){
    r.op=Op::Commit;if(obj.size()!=7||!fields(obj,"v,id,op,transaction,revision,edit,accept_local,")||
      !number64(doc["revision"],r.revision)||!doc["edit"].is<uint32_t>()||!doc["accept_local"].is<bool>())return false;
    r.edit=doc["edit"].as<uint32_t>();r.accept_local=doc["accept_local"].as<bool>();return true;
  }
  if(!doc["profile"].is<uint32_t>())return false;r.profile=doc["profile"].as<uint32_t>();
  if(op=="check"){r.op=Op::Check;return r.profile&&obj.size()==5&&fields(obj,"v,id,op,transaction,profile,");}
  if(op=="forget"){
    r.op=Op::Forget;if(!r.profile||obj.size()!=6||!fields(obj,"v,id,op,transaction,profile,confirm,")||!doc["confirm"].is<bool>())return false;
    r.confirm=doc["confirm"].as<bool>();return true;
  }
  if(op!="stage"||obj.size()!=11||!fields(obj,"v,id,op,transaction,profile,ssid,password,open,keep,priority,enabled,")||
    !text(doc["ssid"],r.ssid)||!text(doc["password"],r.secret)||!doc["open"].is<bool>()||
    !doc["keep"].is<bool>()||!doc["enabled"].is<bool>()||!doc["priority"].is<uint8_t>())return false;
  r.op=Op::Stage;r.open=doc["open"].as<bool>();r.keep=doc["keep"].as<bool>();
  r.enabled=doc["enabled"].as<bool>();r.priority=doc["priority"].as<uint8_t>();return true;
}
} // namespace pro::wifi_setup
