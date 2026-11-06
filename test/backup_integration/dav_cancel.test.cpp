#include "webdav_client.h"
#include "pro_device.h"
#include "usb_storage.h"
#include "koofr_ca.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <type_traits>
#if CSP_ENABLE_BACKUP_SETUP
static_assert(std::is_constructible_v<WebDAVClient,const CloudConfig&>);
static_assert(!std::is_constructible_v<WebDAVClient,CloudConfig&&>);
static_assert(!std::is_constructible_v<WebDAVClient,const CloudConfig&&>);
#endif

uint32_t fakeNow=0;
unsigned liveTLS=0,liveHTTP=0,beginCount=0;
std::deque<FakeReply> fakeReplies;
std::vector<std::string> sentMethods;
std::function<void(unsigned)> afterBegin,afterRequest;
std::function<void(unsigned,size_t)> duringRead;
std::function<void()> duringUpload;
static bool paused=false;
static unsigned openedFixtures=0,passed=0;
static const std::string FileData="controlled synthetic QA fixture content";
static const std::string TestData="Cloud Stick Pro upload and readback test.\n";
static const std::string Properties=
  "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:propstat><d:prop>"
  "<d:resourcetype/><d:getetag>\"123\"</d:getetag></d:prop>"
  "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>";

bool maintenance_active(){return paused;}
bool storage_ready(){return true;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){}
void backup_progress(uint64_t,uint64_t){}

// Actual adapter file operations use an anonymous synthetic fixture. No /sdcard
// mount, user file, device, private credential or real HTTP connection is opened.
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
  assert(std::string(path)=="/sdcard/QA/f.pdf"&&std::string(mode)=="rb");
  ++openedFixtures;FILE* f=std::tmpfile();assert(f);
  assert(std::fwrite(FileData.data(),1,FileData.size(),f)==FileData.size());
  std::rewind(f);return f;
}

CloudConfig configuration(bool koofr=false){
  CloudConfig c;c.is_valid=true;c.server_url=koofr?KoofrServer:"https://dav.example.test/QA/";
  c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;
}
void reset(){
  assert(!liveHTTP&&!liveTLS);
  fakeReplies.clear();sentMethods.clear();beginCount=openedFixtures=0;
  afterBegin={};afterRequest={};duringRead={};duringUpload={};paused=false;
}
void done(const char* name){assert(!liveHTTP&&!liveTLS);++passed;std::cout<<"PASS "<<(CSP_ENABLE_BACKUP_SETUP?"combined: ":"legacy compatibility: ")<<name<<'\n';}
void cancelled(const pro::Result& result){assert(!result.ok&&result.code==pro::SnapshotChanged);}
void compare(const pro::Result& result,size_t enabledCalls,size_t defaultCalls,bool defaultSuccess=true){
#if CSP_ENABLE_BACKUP_SETUP
  (void)defaultCalls;(void)defaultSuccess;cancelled(result);assert(sentMethods.size()==enabledCalls);
#else
  (void)enabledCalls;assert(result.ok==defaultSuccess);assert(sentMethods.size()==defaultCalls);
#endif
}

int main(){
  reset();fakeReplies={{"PROPFIND",207,""}};
  {auto config=configuration();WebDAVClient remote(config);auto r=remote.checkConnection();assert(r.ok&&r.code==207);}
  assert(beginCount==1);done("ordinary authenticated connection check");

  reset();paused=true;fakeReplies={{"PROPFIND",207,""}};
  {auto config=configuration();WebDAVClient remote(config);compare(remote.checkConnection(),0,1);}
#if CSP_ENABLE_BACKUP_SETUP
  assert(beginCount==0);
#endif
  done("retirement before TLS admission");

  reset();fakeReplies={{"PROPFIND",207,""}};
  afterBegin=[](unsigned){paused=true;};
  {auto config=configuration();WebDAVClient remote(config);compare(remote.checkConnection(),0,1);}
  done("retirement between begin and first request");

  for(unsigned stopAt:{1U,2U}){
    reset();fakeReplies={{"PROPFIND",404,""},{"MKCOL",201,""},{"PROPFIND",207,""}};
    afterRequest=[stopAt](unsigned n){if(n==stopAt)paused=true;};
    {auto config=configuration(true);WebDAVClient remote(config);compare(remote.checkConnection(),stopAt,3);}
    done(stopAt==1?"retirement before collection creation":"retirement before repeated PROPFIND");
  }

  reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"MKCOL",201,""}};
  afterRequest=[](unsigned n){if(n==1)paused=true;};
  {auto config=configuration();WebDAVClient remote(config);auto r=remote.put("QA/sub/f.pdf",FileData.size());cancelled(r);}
  assert(sentMethods.size()==(CSP_ENABLE_BACKUP_SETUP?1U:3U)&&openedFixtures==0);
  done("retirement stops parent collection chain before file open");

  reset();fakeReplies={{"PUT",201,""},{"GET",200,TestData}};
  afterRequest=[](unsigned n){if(n==1)paused=true;};
  {auto config=configuration();WebDAVClient remote(config);char path[72]{};compare(remote.testConnection(path,sizeof path),1,2);}
  done("retirement after synthetic PUT stops subsequent GET");

  reset();fakeReplies={{"PUT",201,""},{"GET",200,TestData}};
  duringRead=[](unsigned,size_t bytes){if(bytes>=16)paused=true;};
  {auto config=configuration();WebDAVClient remote(config);char path[72]{};compare(remote.testConnection(path,sizeof path),2,2);}
  done("retirement during formerly unguarded synthetic readback");

  reset();fakeReplies={{"GET",200,FileData}};
  duringRead=[](unsigned,size_t bytes){if(bytes>=16)paused=true;};
  {auto config=configuration();WebDAVClient remote(config);cancelled(remote.verify("QA/f.pdf",hash_text(FileData),FileData.size()));}
  assert(sentMethods.size()==1);done("guarded readback cannot verify after retirement");

  reset();fakeReplies={{"GET",200,FileData},{"PROPFIND",207,Properties}};
  duringRead=[](unsigned request,size_t bytes){if(request==2&&bytes>=16)paused=true;};
  {auto config=configuration();WebDAVClient remote(config);cancelled(remote.verify("QA/f.pdf",hash_text(FileData),FileData.size()));}
  assert(sentMethods.size()==2);done("property-body cancellation prevents publication");

  reset();fakeReplies={{"GET",200,FileData},{"PROPFIND",207,Properties},{"MOVE",201,""},{"GET",200,FileData}};
  {auto config=configuration();WebDAVClient remote(config);auto r=remote.verify("QA/f.pdf",hash_text(FileData),FileData.size());assert(r.ok&&r.code==200);}
  assert(sentMethods.size()==4);done("ordinary full-hash and conditional publication remains usable");

  reset();fakeReplies={{"GET",200,FileData},{"PROPFIND",207,Properties},{"PUT",200,""},{"GET",200,FileData},{"DELETE",204,""}};
  afterBegin=[](unsigned n){if(n==5)paused=true;};
  {auto config=configuration(true);WebDAVClient remote(config);compare(remote.verify("QA/f.pdf",hash_text(FileData),FileData.size()),4,5);}
  assert(openedFixtures==1);done("retirement before optional cleanup sends no DELETE or false success");

  reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
  duringUpload=[](){paused=true;};
  {auto config=configuration();WebDAVClient remote(config);cancelled(remote.put("QA/f.pdf",FileData.size()));}
  assert(sentMethods.size()==3&&openedFixtures==1);done("retirement during file upload retains local fixture");

  reset();fakeReplies={{"GET",200,FileData},{"PROPFIND",207,Properties},{"MOVE",201,""}};
  afterRequest=[](unsigned n){if(n==3)paused=true;};
  {auto config=configuration();WebDAVClient remote(config);cancelled(remote.verify("QA/f.pdf",hash_text(FileData),FileData.size()));}
  assert(sentMethods.size()==3);done("retirement after MOVE cannot claim final verification");

  for(int code:{401,307}){
    reset();fakeReplies={{"PROPFIND",code,""}};
    {auto config=configuration();WebDAVClient remote(config);auto r=remote.checkConnection();assert(!r.ok&&r.code==code);}
    assert(sentMethods.size()==1);done(code==401?"authentication rejection preserved":"redirect rejection preserved");
  }
  std::cout<<passed<<"/"<<passed<<" passed; setup flag "<<CSP_ENABLE_BACKUP_SETUP<<"; synthetic HTTP/TLS/SD only\n";
}
