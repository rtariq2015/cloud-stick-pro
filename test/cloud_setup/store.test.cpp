#include <owner_cloud_store.h>
#include "owner_cloud_nvs.h"
#include "koofr_ca.h"
#include "config_loader.h"
#include <ArduinoJson.h>
#include <nvs.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>
using namespace pro::owner_cloud;
using pro::wifi_setup::Read;

// Fail only the explicit nothrow arrays used by Blob/Shape. Delegate successful
// allocations to the real tracked array allocator, preserving ASan pairing.
static int failArrayAt=-1;
void* operator new[](size_t size,const std::nothrow_t&)noexcept{
  if(failArrayAt==0){failArrayAt=-1;return nullptr;}
  if(failArrayAt>0)--failArrayAt;
  try{return ::operator new[](size);}catch(...){return nullptr;}
}
namespace {
unsigned cases=0;
void pass(const char* name){++cases;std::cout<<"PASS "<<name<<'\n';}
struct Fake final:pro::wifi_setup::BlobStore {
  std::string raw;bool present=false,writeOk=true,applyWrite=true;
  unsigned reads=0,writes=0,failReadAt=0,mutateAt=0;
  bool shortReadback=false,corruptReadback=false;
  Read read(const char* key,uint8_t* out,size_t capacity,size_t& length)override{
    assert(!std::strcmp(key,"config")&&capacity==BlobLimit&&out);++reads;length=0;
    if(reads==failReadAt)return Read::Error;
    if(!present)return Read::Missing;
    if(reads==mutateAt){raw.insert(raw.size()-1,",\"external_change\":true");}
    if(raw.empty()||raw.size()>capacity)return Read::Error;
    length=raw.size();std::memcpy(out,raw.data(),length);
    if(writes&&shortReadback)--length;
    if(writes&&corruptReadback)out[length-2]^=1;
    return Read::Found;
  }
  bool write(const char* key,const uint8_t* bytes,size_t length)override{
    assert(!std::strcmp(key,"config")&&length&&length<=BlobLimit);++writes;
    if(applyWrite){raw.assign(reinterpret_cast<const char*>(bytes),length);present=true;}return writeOk;
  }
};
struct Auth {unsigned calls=0,denyAt=0;};
bool authorized(void* p){auto& value=*static_cast<Auth*>(p);return ++value.calls!=value.denyAt;}
Values credentials(){Values values;std::strcpy(values.username.data(),"synthetic-owner@example.invalid");std::strcpy(values.password.data(),"synthetic-private-app-password");std::strcpy(values.folder.data(),"CloudStickPro/Device_A");return values;}
pro::wifi_setup::StoredProfile profile(){pro::wifi_setup::StoredProfile p;p.id=1;std::strcpy(p.ssid.data(),"Synthetic AP");std::strcpy(p.secret.data(),"synthetic-wifi-password");return p;}
std::string legacy(bool koofr=true){
  JsonDocument doc;doc["schema"]=1;doc["mode"]="upload_only";
  doc["server_url"]=koofr?"https://app.koofr.net/dav/Koofr/Previous/":"https://other.synthetic.invalid/dav/";
  doc["username"]="synthetic-saved-user";doc["password"]="synthetic-saved-password";doc["ca_pem"]=KoofrCA;doc["ntp_server"]="time.synthetic.invalid";
  doc["ssid"]="legacy-network-must-stay";doc["wifi_password"]="legacy-secret-must-stay";doc["wifi_open"]=false;
  doc["watch_folder"]="QA_EXISTING";doc["backup_mode"]="overnight";doc["idle_seconds"]=345;doc["timezone"]="UTC0";
  doc["window_start"]=22;doc["window_end"]=6;doc["sync_interval_s"]=180;doc["max_file_size_mb"]=128;
  auto unknown=doc["unknown_setting"].to<JsonObject>();unknown["keep"]=true;auto rows=unknown["rows"].to<JsonArray>();rows.add(7);rows.add(nullptr);
  std::string raw;serializeJson(doc,raw);return raw;
}
JsonDocument parse(const std::string& raw){JsonDocument doc;assert(!deserializeJson(doc,raw));return doc;}
Fake saved(bool koofr=true){Fake fake;fake.present=true;fake.raw=legacy(koofr);return fake;}
void nvsReset(bool present=false,std::string value={}){
  using namespace config_fake;assert(!opened);blob=std::move(value);pending.clear();config_fake::present=present;
  applySetError=applyCommitError=corruptReadback=false;openResult=queryResult=readResult=setResult=commitResult=ESP_OK;
  useQueryOverride=useReadOverride=false;opens=closes=queries=reads=writes=erases=commits=0;
}
}
int main(int argc,char** argv){
  const auto selected=profile();auto values=credentials();
  if(argc==2&&!std::strcmp(argv[1],"--native-large-pool")){
    // Native64 uses a4096-byte variant pool vs1024 bytes on ESP32. Its larger
    // first pool plus the PEM parser's copy/grow peak exceeds our unchanged8KiB
    // document budget. Keep this real allocation rejection visible and prove
    // it happens before the first persistent write, rather than enlarging it.
    assert(ARDUINOJSON_POOL_CAPACITY==256);
    Fake fake;Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Missing);
    PublicSettings applied;Auth auth;
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Unavailable);
    assert(!fake.present&&fake.writes==0&&applied.revision==1);
    auto previous=saved();Store prior(previous,KoofrCA);Record record;const auto old=previous.raw;
    const auto loaded=prior.load(record);assert(loaded==Loaded::Valid||loaded==Loaded::Unavailable);
    assert(previous.raw==old&&previous.writes==0);
    pass("default64-bit fresh-record JSON pool exhaustion is explicit before write; legacy read preserves previous bytes");return 0;
  }
  assert(argc==1&&ARDUINOJSON_POOL_CAPACITY==128);
  {
    Fake fake;Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Missing);
    assert(initial.missing&&initial.info.revision==1&&!initial.info.configured&&fake.writes==0);
    PublicSettings applied;Auth auth;assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Committed);
    auto doc=parse(fake.raw);assert(doc["schema"]==1&&doc["cloud_rev"]==2&&doc["mode"]=="upload_only");
    assert(doc["ssid"]==selected.ssid.data()&&doc["wifi_password"]==selected.secret.data()&&doc["wifi_open"]==false);
    assert(doc["server_url"]=="https://app.koofr.net/dav/Koofr/CloudStickPro/Device_A/"&&doc["ca_pem"]==KoofrCA);
    assert(doc["watch_folder"]=="QA"&&doc["backup_mode"]=="idle"&&doc["idle_seconds"]==120);
    assert(applied.revision==2&&std::string(applied.ntp_server.data())=="pool.ntp.org"&&fake.writes==1);
    Record after;const auto loaded=store.load(after);
    if(loaded!=Loaded::Valid||after.info.revision!=2||!after.info.configured||after.info.provider!=Provider::Koofr)
      std::cerr<<"Reload diagnostic: state="<<unsigned(loaded)<<" revision="<<after.info.revision<<" configured="<<after.info.configured<<" provider="<<unsigned(after.info.provider)<<" raw_bytes="<<fake.raw.size()<<'\n';
    assert(loaded==Loaded::Valid&&after.info.revision==2&&after.info.configured&&after.info.provider==Provider::Koofr);
    nvsReset(true,fake.raw);const auto worker=load_backup_config();const auto compatible=load_config();
    assert(worker.is_valid&&compatible.is_valid&&worker.username==values.username.data()&&worker.password==values.password.data());
    assert(compatible.ssid==selected.ssid.data()&&compatible.wifi_password==selected.secret.data()&&worker.server_url==compatible.server_url);
    pass("new board: missing read stays read-only; private commit initializes only reviewed defaults and selected legacy Wi-Fi");
  }
  {
    auto maximal=credentials();std::fill_n(maximal.username.data(),128,'"');std::fill_n(maximal.password.data(),128,'\\');std::fill_n(maximal.folder.data(),96,'A');
    assert(complete(maximal));Fake fake;Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Missing);
    PublicSettings applied;Auth auth;assert(store.publish(initial,maximal,&selected,1,authorized,&auth,applied)==Saved::Committed);
    nvsReset(true,fake.raw);const auto worker=load_backup_config();const auto compatible=load_config();
    assert(worker.is_valid&&compatible.is_valid&&worker.username==maximal.username.data()&&worker.password==maximal.password.data());
    Record fresh;assert(store.load(fresh)==Loaded::Valid&&fresh.info.configured&&fresh.initial.username==maximal.username&&fresh.initial.password==maximal.password&&fresh.initial.folder==maximal.folder);
    pass("maximum escaped credentials and96-byte folder roundtrip through actual worker, legacy loader and owner codec");
  }
  {
    auto fake=saved();Store store(fake,KoofrCA);Record initial;const auto old=fake.raw;
    assert(store.load(initial)==Loaded::Valid&&fake.raw==old&&fake.writes==0&&initial.info.revision==1);
    assert(initial.info.configured&&initial.info.has_username&&initial.info.has_password&&initial.info.has_folder);
    auto before=parse(old);PublicSettings applied;Auth auth;
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Committed);
    auto after=parse(fake.raw);
    for(const char* key:{"ssid","wifi_password","wifi_open","watch_folder","backup_mode","idle_seconds","timezone","window_start","window_end","sync_interval_s","max_file_size_mb","ntp_server","unknown_setting"})assert(after[key]==before[key]);
    assert(after["cloud_rev"]==2&&after["username"]==values.username.data()&&applied.revision==2);
    pass("legacy revision1 read has no migration write; update preserves every unknown, Wi-Fi and schedule value");
  }
  {
    auto fake=saved(false);Store store(fake,KoofrCA);Record initial;
    assert(store.load(initial)==Loaded::Valid&&initial.info.configured&&initial.info.provider==Provider::Other);
    assert(!initial.info.has_username&&!initial.info.has_password&&!initial.info.has_folder);
    assert(!initial.initial.username[0]&&!initial.initial.password[0]&&!initial.initial.folder[0]);
    pass("other providers never import credentials into the Koofr candidate");
  }
  {
    auto fake=saved();Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);const auto original=fake.raw;
    PublicSettings applied;Auth auth;assert(store.publish(initial,values,&selected,2,authorized,&auth,applied)==Saved::Conflict&&fake.raw==original&&fake.writes==0);
    fake.raw.insert(fake.raw.size()-1,",\"external_change\":true");
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Conflict&&fake.writes==0);
    pass("CAS detects stale revision and a nonrevision legacy change");
  }
  {
    auto fake=saved();Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);
    fake.mutateAt=fake.reads+2;PublicSettings applied;Auth auth;
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Conflict&&fake.writes==0);
    pass("final full-value CAS detects a change during preparation");
  }
  for(unsigned denial:{1u,2u}){
    auto fake=saved();Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);const auto original=fake.raw;
    PublicSettings applied;Auth auth;auth.denyAt=denial;
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Cancelled&&fake.raw==original&&fake.writes==0);
  }pass("authorization is rechecked before the write; precommit cancellation preserves the old value exactly");
  for(int failure=0;failure<4;++failure){
    auto fake=saved();Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);
    if(failure==0)fake.writeOk=false;if(failure==1)fake.shortReadback=true;if(failure==2)fake.corruptReadback=true;if(failure==3)fake.failReadAt=fake.reads+3;
    PublicSettings applied;Auth auth;
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Unknown&&fake.writes==1&&applied.revision==1);
  }pass("write/commit acknowledgment and every exact readback failure are unknown, never rolled back or called durable");
  for(const auto* prefix:{"http://app.koofr.net/dav/Koofr/Previous/","https://app.koofr.net.evil.invalid/dav/Koofr/Previous/","https://app.koofr.net/dav/Koofr/../bad/","https://app.koofr.net/dav/Koofr/Previous/?x=1"}){
    auto fake=saved();auto doc=parse(fake.raw);doc["server_url"]=prefix;fake.raw.clear();serializeJson(doc,fake.raw);
    Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid&&initial.info.provider==Provider::Other&&!initial.info.has_password);
  }pass("endpoint recognition excludes insecure, deceptive-host, traversal and query destinations");
  {
    auto fake=saved();Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);const auto old=fake.raw;
    Values bad=credentials();std::strcpy(bad.folder.data(),"https://evil.invalid/");PublicSettings applied;Auth auth;
    assert(store.publish(initial,bad,&selected,1,authorized,&auth,applied)==Saved::Invalid&&fake.raw==old&&fake.writes==0);
    pass("candidate folder cannot become an arbitrary endpoint");
  }
  for(const char* blob:{"{}","[]","null","{broken","{\"schema\":1,\"schema\":1,\"mode\":\"upload_only\"}","{\"schema\":1,\"mode\":\"upload_only\",\"clo\\u0075d_rev\":1}","{\"schema\":1,\"mode\":\"upload_only\",\"cloud_rev\":true}","{\"schema\":1,\"mode\":\"upload_only\",\"cloud_rev\":\"1\"}","{\"schema\":1,\"mode\":\"upload_only\",\"cloud_rev\":0}","{\"schema\":1,\"mode\":\"upload_only\",\"username\":\"bad\\u0000value\"}","{\"schema\":1,\"mode\":\"upload_only\",\"x\":1e999}","{\"schema\":1,\"mode\":\"upload_only\",\"x\":18446744073709551616}","{\"schema\":1,\"mode\":\"upload_only\",\"x\":{\"y\":{\"z\":{}}}}","{\"schema\":1,\"mode\":\"upload_only\",\"x\":{\"a\":1,\"a\":2}}","{\"schema\":1,\"mode\":\"upload_only\",\"password\":\"bad\\uD800\"}"}){
    Fake fake;fake.present=true;fake.raw=blob;const auto old=fake.raw;Store store(fake,KoofrCA);Record initial;
    assert(store.load(initial)==Loaded::Corrupt&&fake.raw==old&&fake.writes==0);
  }pass("strict legacy JSON rejects malformed shapes, duplicate/aliased keys, wrong revision types, NUL, numeric overflow and excessive depth");
  {
    auto fake=saved();auto doc=parse(fake.raw);doc["cloud_rev"]=UINT64_MAX;fake.raw.clear();serializeJson(doc,fake.raw);
    Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid&&initial.info.revision==UINT64_MAX);
    PublicSettings applied;Auth auth;assert(store.publish(initial,values,&selected,UINT64_MAX,authorized,&auth,applied)==Saved::Exhausted&&fake.writes==0);
    pass("revision exhaustion is terminal before any write");
  }
  for(int allocation=0;allocation<4;++allocation){
    auto fake=saved();Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);const auto old=fake.raw;
    failArrayAt=allocation;PublicSettings applied;Auth auth;
    assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Unavailable);failArrayAt=-1;
    assert(fake.raw==old&&fake.writes==0);
  }pass("explicit buffer/shape/image allocation failures preserve the previous valid value");
  {
    Fake fake;Store store(fake,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Missing);
    PublicSettings applied;Auth auth;assert(store.publish(initial,values,nullptr,1,authorized,&auth,applied)==Saved::Invalid&&!fake.present&&fake.writes==0);
    pass("first enrollment requires a valid selected Wi-Fi profile for legacy compatibility");
  }
  {
    OwnerCloudNvs adapter;nvsReset();Record initial;Store store(adapter,KoofrCA);
    assert(store.load(initial)==Loaded::Missing&&config_fake::writes==0&&!config_fake::opened);
    PublicSettings applied;Auth auth;assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Committed);
    assert(config_fake::writes==1&&config_fake::commits==1&&config_fake::erases==0&&!config_fake::opened);
    size_t size=1;uint8_t byte=0;const auto opens=config_fake::opens;
    assert(adapter.read("owner",&byte,1,size)==Read::Error&&size==0&&!adapter.write("owner",&byte,1)&&config_fake::opens==opens);
    pass("actual NVS adapter is constrained to csp/config; one commit and no erase/reset operation");
  }
  for(int failure=0;failure<3;++failure){
    nvsReset(true,legacy());OwnerCloudNvs adapter;Store store(adapter,KoofrCA);Record initial;assert(store.load(initial)==Loaded::Valid);
    if(failure==0){config_fake::setResult=ESP_FAIL;config_fake::applySetError=true;}
    if(failure==1){config_fake::commitResult=ESP_FAIL;config_fake::applyCommitError=true;}
    if(failure==2)config_fake::corruptReadback=true;
    PublicSettings applied;Auth auth;assert(store.publish(initial,values,&selected,1,authorized,&auth,applied)==Saved::Unknown);
    assert(config_fake::writes==1&&config_fake::erases==0&&!config_fake::opened);
  }pass("actual NVS set/commit failure after application and corrupted readback remain unknown");
  {
    nvsReset(true,legacy());OwnerCloudNvs adapter;Store store(adapter,KoofrCA);Record initial;
    config_fake::useReadOverride=true;config_fake::readOverride=config_fake::blob.size()-1;
    assert(store.load(initial)==Loaded::Unavailable&&config_fake::writes==0&&!config_fake::opened);
    nvsReset(true,std::string(BlobLimit+1,'X'));
    assert(store.load(initial)==Loaded::Unavailable&&config_fake::writes==0&&!config_fake::opened);
    pass("actual NVS adapter rejects short and oversized values before decoding");
  }
  std::cout<<"Native sizes: Record="<<sizeof(Record)<<" Values="<<sizeof(Values)<<" Info="<<sizeof(Info)
    <<" Store="<<sizeof(Store)<<" NVS="<<sizeof(OwnerCloudNvs)<<" PublicSettings="<<sizeof(PublicSettings)
    <<" CloudConfig="<<sizeof(CloudConfig)<<" ShapeFrames="<<3*(sizeof(std::array<std::string_view,64>)+sizeof(size_t))<<'\n';
  std::cout<<"PASS "<<cases<<" cloud storage groups; synthetic data only; no network/device\n";
}
