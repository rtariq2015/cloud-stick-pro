// Actual DAV adapter and Backup/receipt codec. HTTP/TLS/SD/time are simulated;
// no user files, cloud account or physical stick are accessed.
#include "webdav_client.h"
#include "pro_device.h"
#include "usb_storage.h"
#include <cassert>
#include <iostream>
#include <map>
uint32_t fakeNow=0;
unsigned liveTLS=0,liveHTTP=0,beginCount=0;
std::deque<FakeReply> fakeReplies;
std::vector<std::string> sentMethods;
std::function<void(unsigned)> afterBegin,afterRequest;
std::function<void(unsigned,size_t)> duringRead;
std::function<void()> duringUpload;
static bool paused=false,media=true;
static unsigned cases=0,opens=0;
static const std::string Data="Synthetic batch fixture, no personal content.";
static const std::string Properties=
 "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:propstat><d:prop>"
 "<d:resourcetype/><d:getetag>\"123\"</d:getetag></d:prop>"
 "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>";
bool maintenance_active(){return paused;}
bool storage_ready(){return media;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){}
void backup_progress(uint64_t,uint64_t){}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
 assert(std::string(path).rfind("/sdcard/QA/",0)==0&&std::string(mode)=="rb");
 ++opens;FILE* f=std::tmpfile();assert(f);
 assert(std::fwrite(Data.data(),1,Data.size(),f)==Data.size());std::rewind(f);return f;
}
static CloudConfig config(){CloudConfig c;c.is_valid=true;c.server_url="https://dav.example.test/backup/";c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;}
static void reset(){assert(!liveTLS&&!liveHTTP);fakeNow=0;beginCount=opens=0;fakeReplies.clear();sentMethods.clear();collection_test::requests.clear();collection_test::failBodyRequest=0;paused=false;media=true;afterBegin={};afterRequest={};duringRead={};duringUpload={};}
static void done(const char* label){assert(!liveTLS&&!liveHTTP&&fakeReplies.empty());++cases;std::cout<<"PASS "<<label<<'\n';}
static void collections(unsigned count,int code=201){for(unsigned i=0;i<count;++i)fakeReplies.push_back({"MKCOL",code,""});}
static void upload(){fakeReplies.push_back({"PUT",201,""});}
static void verified(bool existing=false){fakeReplies.insert(fakeReplies.end(),{{"GET",200,Data},{"PROPFIND",existing?207:404,existing?Properties:""},{"MOVE",201,""},{"GET",200,Data}});}
static unsigned count(const char* method){return unsigned(std::count(sentMethods.begin(),sentMethods.end(),method));}
static void ok(WebDAVClient& remote,const std::string& path){assert(remote.put(path,Data.size()).ok);assert(remote.verify(path,hash_text(Data),Data.size()).ok);}
static unsigned setupAfterWarm(unsigned parents=1){return CSP_ENABLE_DAV_COLLECTION_CACHE?0:parents+1;}
struct Local:pro::Local {
 bool fingerprint(const std::string&,std::string& hash,uint64_t& size)override{hash=hash_text(Data);size=Data.size();return true;}
 std::string key(const std::string& path)override{return hash_text(path);}
};
struct Journal:pro::Journal {
 std::map<std::string,pro::Record> rows;unsigned transfers=0,verified=0;
 pro::Load load(const std::string& key,pro::Record& out)override{auto it=rows.find(key);if(it==rows.end())return pro::Load::Missing;out=it->second;return pro::Load::Found;}
 bool save(const std::string& key,const pro::Record& in)override{uint8_t bytes[pro::RecordSize];pro::Record out;assert(pro::encodeRecord(in,bytes)&&pro::decodeRecord(bytes,out));rows[key]=out;if(out.state==pro::State::Transferred)++transfers;if(out.state==pro::State::Verified)++verified;return true;}
};
int main(){
 reset();{
  const auto c=config();WebDAVClient remote(c);Local local;Journal journal;pro::Backup backup(local,journal,remote,std::string(64,'a'));
  for(unsigned i=0;i<10;++i){
   collections(i?setupAfterWarm(3):4);upload();verified();
   const auto result=backup.process("QA/sub/deep/fixture-"+std::to_string(i)+".bin",1767225600);
   assert(result.outcome==pro::Outcome::Verified&&result.transferred);
  }
  assert(journal.verified==10&&opens==10&&count("PUT")==10&&count("GET")==20&&count("MOVE")==10&&count("PROPFIND")==10);
  assert(count("MKCOL")==unsigned(CSP_ENABLE_DAV_COLLECTION_CACHE?4:40));
  assert(sentMethods.size()==unsigned(CSP_ENABLE_DAV_COLLECTION_CACHE?54:90));
  std::cout<<"BATCH files=10 setup_requests="<<count("MKCOL")<<" total_requests="<<sentMethods.size()<<" cache="<<CSP_ENABLE_DAV_COLLECTION_CACHE<<'\n';
 }done("ten distinct files retain PUT, both full readbacks, type check, publication and receipts");

 reset();{
  const auto c=config();WebDAVClient remote(c);collections(2);upload();verified();ok(remote,"QA/a.bin");
  collections(setupAfterWarm());upload();verified(true);ok(remote,"QA/b.bin");
  const auto move=std::find(sentMethods.rbegin(),sentMethods.rend(),"MOVE").base()-sentMethods.begin()-1;
  const auto& headers=collection_test::requests[size_t(move)].headers;
  assert(headers.at("Overwrite")=="T"&&headers.at("Destination")=="https://dav.example.test/backup/QA/b.bin");
  assert(headers.at("If")=="<https://dav.example.test/backup/QA/b.bin> ([\"123\"])");
 }done("cached collection never removes conditional overwrite/type protections");

 reset();{
  const auto c=config();WebDAVClient remote(c);collections(3);upload();verified();ok(remote,"QA/same/a.bin");
  collections(CSP_ENABLE_DAV_COLLECTION_CACHE?2:3);upload();verified();ok(remote,"QA/same-prefix/b.bin");
  assert(collection_test::requests[8].endpoint=="https://dav.example.test/backup/QA/");
 }done("different exact parent cannot share cached permission, including common prefixes");

 for(int error:{401,403,404,409,429,500,-1}){
  reset();const auto c=config();WebDAVClient remote(c);collections(2);upload();verified();ok(remote,"QA/a.bin");
  collections(setupAfterWarm());fakeReplies.push_back({"PUT",error,""});auto result=remote.put("QA/b.bin",Data.size());assert(!result.ok&&result.code==error);
  collections(2);upload();verified();ok(remote,"QA/b.bin");done("failed PUT invalidates setup hints; one normal retry recreates folders");
 }

 reset();{
  const auto c=config();WebDAVClient remote(c);Local local;Journal journal;pro::Backup backup(local,journal,remote,std::string(64,'a'));
  collections(2);upload();verified();assert(backup.process("QA/a.bin",1767225600).outcome==pro::Outcome::Verified);
  collections(setupAfterWarm());upload();fakeReplies.insert(fakeReplies.end(),{{"GET",200,Data},{"PROPFIND",404,""},{"MOVE",409,""}});
  auto result=backup.process("QA/b.bin",1767225601);assert(result.outcome==pro::Outcome::Failed&&result.error==409);
  assert(journal.rows.at(hash_text("QA/b.bin")).state==pro::State::Transferred);
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  fakeReplies.push_back({"GET",200,Data});collections(1);fakeReplies.insert(fakeReplies.end(),{{"PROPFIND",404,""},{"MOVE",201,""},{"GET",200,Data}});
  const auto puts=count("PUT");result=backup.process("QA/b.bin",1767229202);
  assert(result.outcome==pro::Outcome::Verified&&!result.transferred&&count("PUT")==puts&&journal.verified==2);
#endif
 }done("lost destination parent never marks Verified; retry repairs parent without another PUT");

 for(bool final:{false,true}){
  reset();const auto c=config();WebDAVClient remote(c);Local local;Journal journal;pro::Backup backup(local,journal,remote,std::string(64,'a'));
  collections(2);upload();verified();assert(backup.process("QA/a.bin",1767225600).outcome==pro::Outcome::Verified);
  collections(setupAfterWarm());upload();
  if(final)fakeReplies.insert(fakeReplies.end(),{{"GET",200,Data},{"PROPFIND",404,""},{"MOVE",201,""}});
  fakeReplies.push_back({"GET",200,std::string(Data.size(),'!')});auto r=backup.process("QA/b.bin",1767225601);
  assert(r.outcome==pro::Outcome::Failed&&r.error==pro::DigestMismatch&&journal.verified==1);
  assert(journal.rows.at(hash_text("QA/b.bin")).state==pro::State::Pending);
  collections(2);upload();verified();ok(remote,"QA/b.bin");done("staged/final digest mismatch rejects receipt and discards setup hints");
 }

 for(bool retire:{false,true}){
  reset();const auto c=config();WebDAVClient remote(c);collections(2);upload();verified();ok(remote,"QA/a.bin");
  if(retire)paused=true;else media=false;const auto before=sentMethods.size();auto r=remote.put("QA/b.bin",Data.size());
  assert(!r.ok&&r.code==pro::SnapshotChanged&&sentMethods.size()==before&&opens==1);
  paused=false;media=true;collections(2);upload();verified();ok(remote,"QA/b.bin");done("media/worker cancellation wins over a warm cache and clears it");
 }

 reset();{
  const auto c=config();{WebDAVClient remote(c);collections(2);upload();verified();ok(remote,"QA/a.bin");}
  {WebDAVClient replacement(c);collections(2);upload();verified();ok(replacement,"QA/b.bin");}
  assert(count("MKCOL")==4);
 }done("new worker has no retained collection cache");

 reset();{
  const auto c=config();WebDAVClient remote(c);collections(2,405);fakeReplies.push_back({"PUT",409,""});
  assert(!remote.put("QA/a.bin",Data.size()).ok);collections(2);upload();verified();ok(remote,"QA/a.bin");
 }done("405 collection responses never substitute for successful file upload/readback");

 reset();{
  const auto c=config();WebDAVClient remote(c);const std::string parent="QA/"+std::string(pro::PathLimit-9,'x')+"/";
  assert(pro::validPath(parent+"a.bin"));collections(3);upload();verified();ok(remote,parent+"a.bin");
  collections(setupAfterWarm(2));upload();verified();ok(remote,parent+"b.bin");
 }done("maximum-size exact parent is bounded under sanitizers");

 for(bool final:{false,true}){
  reset();const auto c=config();WebDAVClient remote(c);collections(2);upload();assert(remote.put("QA/a.bin",Data.size()).ok);
  if(final)verified();else fakeReplies.push_back({"GET",200,Data});
  collection_test::failBodyRequest=final?7:4;
  const auto result=remote.verify("QA/a.bin",hash_text(Data),Data.size());assert(!result.ok&&result.code==-4);
#if CSP_ENABLE_CLOUD_REQUEST_TRACE
  const auto trace=cloud_request_snapshot();assert(trace.enabled&&!trace.active);
  assert(trace.last.operation==(final?CloudRequestOp::FinalReadback:CloudRequestOp::StageReadback));
  assert(trace.last.http_code==200&&trace.last.body_result==-4&&trace.last.result_code==-4);
  assert(trace.last_negative.id==trace.last.id);
#endif
  done("body failure is preserved and distinguished between staging/final readback");
 }
 reset();{
  const auto c=config();WebDAVClient remote(c);fakeReplies={{"GET",404,""},{"GET",-4,""}};
  auto r=remote.verify("QA/a.bin",hash_text(Data),Data.size());assert(!r.ok&&r.code==-4);
#if CSP_ENABLE_CLOUD_REQUEST_TRACE
  const auto trace=cloud_request_snapshot();assert(trace.last.operation==CloudRequestOp::FinalReadback);
  assert(trace.last.http_code==-4&&trace.last.body_result==CloudBodyNotRead&&trace.last.result_code==-4);
#endif
 }done("final GET header failure remains distinct from response-body failure");
 reset();{
  const auto c=config();WebDAVClient remote(c);collections(2);upload();assert(remote.put("QA/a.bin",Data.size()).ok);
  fakeReplies={{"GET",200,Data},{"PROPFIND",404,""},{"MOVE",-4,""}};
  auto r=remote.verify("QA/a.bin",hash_text(Data),Data.size());assert(!r.ok&&r.code==-4);
#if CSP_ENABLE_CLOUD_REQUEST_TRACE
  const auto trace=cloud_request_snapshot();assert(trace.last.operation==CloudRequestOp::Publish&&trace.last.http_code==-4&&trace.last.body_result==CloudBodyNotRead);
#endif
 }done("publishing transport failure stays failed without a final GET");
 reset();{
  const auto c=config();WebDAVClient remote(c);collections(2);upload();assert(remote.put("QA/a.bin",Data.size()).ok);
  fakeReplies={{"GET",200,Data},{"PROPFIND",207,Properties}};collection_test::failBodyRequest=5;
  auto r=remote.verify("QA/a.bin",hash_text(Data),Data.size());assert(!r.ok&&r.code==-2011);
#if CSP_ENABLE_CLOUD_REQUEST_TRACE
  const auto trace=cloud_request_snapshot();assert(trace.last.operation==CloudRequestOp::DestinationType);
  assert(trace.last.http_code==207&&trace.last.body_result==-4&&trace.last.result_code==-2011);
#endif
 }done("failed type-check body keeps original destination protection and error mapping");
 std::cout<<cases<<" actual adapter case groups passed; cache="<<CSP_ENABLE_DAV_COLLECTION_CACHE<<"; synthetic HTTP/TLS/SD only\n";
}
