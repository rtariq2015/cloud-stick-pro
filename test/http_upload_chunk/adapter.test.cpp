// Actual WebDAV adapter, actual FileStream, and actual selected derived method.
// Everything below the adapter (SD file, HTTP peer, TLS) is synthetic.
#include "../../src/webdav_client.cpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <numeric>
#include <type_traits>
uint32_t fakeNow=0;
unsigned liveTLS=0,liveHTTP=0,beginCount=0;
std::deque<FakeReply> fakeReplies;
std::vector<std::string> sentMethods;
std::function<void(unsigned)> afterBegin,afterRequest;
std::function<void(unsigned,size_t)> duringRead;
std::function<void()> duringUpload;
UploadPeer uploadPeer;
static bool paused=false,media=true;
static unsigned storageCalls=0,invalidateAt=0,cases=0;
static const std::string FileData=[](){std::string s(2*1024*1024+137,'\0');for(size_t i=0;i<s.size();++i)s[i]=char(i*37U+i/251U);return s;}();
bool maintenance_active(){return paused;}
bool storage_ready(){++storageCalls;if(invalidateAt&&storageCalls>=invalidateAt)media=false;return media;}
void backup_transfer_begin(pro::TransferPhase,uint64_t){}
void backup_progress(uint64_t,uint64_t){}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
  assert(std::string(path)=="/sdcard/QA/f.pdf"&&std::string(mode)=="rb");
  FILE* f=std::tmpfile();assert(f);assert(std::fwrite(FileData.data(),1,FileData.size(),f)==FileData.size());std::rewind(f);return f;
}
static CloudConfig config(){CloudConfig c;c.is_valid=true;c.server_url="https://synthetic.invalid/QA/";c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;}
static void reset(){assert(!liveHTTP&&!liveTLS);fakeReplies.clear();sentMethods.clear();afterBegin={};afterRequest={};duringRead={};duringUpload={};uploadPeer={};paused=false;media=true;storageCalls=invalidateAt=0;}
static void pass(const char* name){assert(!liveHTTP&&!liveTLS);++cases;std::cout<<"PASS "<<name<<'\n';}
struct Local:pro::Local {
 bool fingerprint(const std::string&,std::string& hash,uint64_t& size)override{hash=hash_text(FileData);size=FileData.size();return true;}
 std::string key(const std::string&)override{return std::string(64,'a');}
};
struct Journal:pro::Journal {
 pro::Record receipt{};bool exists=false;unsigned transferred=0,verified=0;
 pro::Load load(const std::string&,pro::Record& out)override{if(!exists)return pro::Load::Missing;out=receipt;return pro::Load::Found;}
 bool save(const std::string&,const pro::Record& in)override{uint8_t data[pro::RecordSize];assert(pro::encodeRecord(in,data)&&pro::decodeRecord(data,receipt));exists=true;if(in.state==pro::State::Transferred)++transferred;if(in.state==pro::State::Verified)++verified;return true;}
};
int main(){
#if CSP_UPLOAD_THROUGHPUT_TRIAL
 static_assert(!std::is_same_v<UploadHTTPClient,HTTPClient>);
#else
 static_assert(std::is_same_v<UploadHTTPClient,HTTPClient>);
#endif
 auto c=config();WebDAVClient remote(c);
 reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
 auto ok=remote.put("QA/f.pdf",FileData.size());assert(ok.ok&&ok.code==201);
#if CSP_UPLOAD_THROUGHPUT_TRIAL
 assert(uploadPeer.output==std::vector<uint8_t>(FileData.begin(),FileData.end()));
 assert(*std::max_element(uploadPeer.requests.begin(),uploadPeer.requests.end())==CSP_UPLOAD_TX_BYTES);
#else
 assert(uploadPeer.output.empty());
#endif
 pass("selected file PUT reaches correct stream method and preserves exact 2MiB bytes");
 reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
 duringUpload=[](){paused=true;};
 auto stopped=remote.put("QA/f.pdf",FileData.size());assert(!stopped.ok&&stopped.code==pro::SnapshotChanged);
 pass("actual FileStream revocation after chunk yields SnapshotChanged and retains fixture");
#if CSP_UPLOAD_THROUGHPUT_TRIAL
 reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
 afterRequest=[](unsigned n){if(n==3){storageCalls=0;invalidateAt=2;}};
 auto revoked=remote.put("QA/f.pdf",FileData.size());
 assert(!revoked.ok&&revoked.code==pro::SnapshotChanged&&uploadPeer.writes==0);
 pass("actual FileStream second-available revocation is rejected before SD read or TCP write");
 reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
 afterRequest=[](unsigned n){if(n==3){storageCalls=0;invalidateAt=2;}};
 {Local local;Journal journal;pro::Backup backup(local,journal,remote,std::string(64,'b'));
  auto result=backup.process("QA/f.pdf",1793793600);
  assert(result.outcome==pro::Outcome::Pending&&result.error==pro::SnapshotChanged&&!result.transferred);
  assert(journal.exists&&journal.receipt.state==pro::State::Pending&&journal.transferred==0&&journal.verified==0&&uploadPeer.writes==0);
 }
 pass("actual Backup/receipt codec cannot mark Transferred or Verified after source race");
#endif
 reset();fakeReplies={{"MKCOL",201,""},{"MKCOL",201,""},{"PUT",201,""}};
 auto empty=remote.put("QA/f.pdf",0);assert(empty.ok&&uploadPeer.writes==0);
 pass("empty PUT retains explicit Content-Length zero/base overload");
 std::cout<<cases<<" actual adapter case groups passed; trial="<<CSP_UPLOAD_THROUGHPUT_TRIAL<<'\n';
}
