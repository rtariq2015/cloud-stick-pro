#include "pro_core.h"
#include "pro_journal.h"
#include "pro_http.h"
#include "pro_diagnostics.h"
#include "pro_snapshot.h"
#include "pro_display.h"
#include "pro_blockio.h"
#include "pro_dav_properties.h"
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>
namespace fs=std::filesystem;
#define REQUIRE(x) do { if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); } while(0)
static std::string digest(const std::string& data) {
  unsigned char b[32];unsigned n;EVP_MD_CTX* c=EVP_MD_CTX_new();EVP_DigestInit_ex(c,EVP_sha256(),nullptr);EVP_DigestUpdate(c,data.data(),data.size());EVP_DigestFinal_ex(c,b,&n);EVP_MD_CTX_free(c);
  static const char hex[]="0123456789abcdef";std::string s;for(unsigned char x:b){s+=hex[x>>4];s+=hex[x&15];}return s;
}
struct Local:pro::Local {
  fs::path root;size_t maxChunk=0;bool broken=false;
  explicit Local(fs::path p):root(p){}
  std::string key(const std::string& p)override{return digest(p);}
  bool fingerprint(const std::string& p,std::string& out,uint64_t& size)override {
    if(broken)return false;
    std::ifstream f(root/p,std::ios::binary);if(!f)return false;
    EVP_MD_CTX* c=EVP_MD_CTX_new();EVP_DigestInit_ex(c,EVP_sha256(),nullptr);size=0;char chunk[pro::ChunkSize];
    while(f){f.read(chunk,sizeof chunk);size_t n=f.gcount();maxChunk=std::max(maxChunk,n);EVP_DigestUpdate(c,chunk,n);size+=n;}
    unsigned char bytes[32];unsigned n;EVP_DigestFinal_ex(c,bytes,&n);EVP_MD_CTX_free(c);static const char hex[]="0123456789abcdef";out.clear();for(unsigned char b:bytes){out+=hex[b>>4];out+=hex[b&15];}return f.eof();
  }
};
struct Journal:pro::FileJournal {
  unsigned writes=0,failAt=0;bool fail=false;
  explicit Journal(fs::path p):FileJournal(p.string()){}
  bool save(const std::string& k,const pro::Record& r)override { ++writes;if(fail||writes==failAt)return false;return FileJournal::save(k,r); }
};
struct Remote:pro::Remote {
  Local& local;Local cloud;int puts=0,gets=0;int putError=0,getError=0;bool corrupt=false;
  Remote(Local& l,fs::path p):local(l),cloud(p){}
  pro::Result put(const std::string& p,uint64_t)override {
    ++puts;if(putError)return {false,putError};fs::create_directories((cloud.root/p).parent_path());fs::copy_file(local.root/p,cloud.root/p,fs::copy_options::overwrite_existing);
    if(corrupt){std::ofstream f(cloud.root/p,std::ios::binary|std::ios::trunc);f<<"bad";}return {true,201};
  }
  pro::Result verify(const std::string& p,const std::string& expected,uint64_t size)override {
    ++gets;if(getError)return {false,getError};std::string h;uint64_t n;
    if(!cloud.fingerprint(p,h,n))return {false,404};return {h==expected&&n==size,h==expected&&n==size?200:pro::DigestMismatch};
  }
};
struct Fixture {
  fs::path root;Local local;Journal journal;Remote remote;pro::Backup backup;
  static fs::path tmp(){char name[]="/tmp/cloud-stick-pro-XXXXXX";char* p=mkdtemp(name);if(!p)throw std::runtime_error("mkdtemp");return p;}
  Fixture():root(tmp()),local(root/"local"),journal(root/"journal"),remote(local,root/"remote"),backup(local,journal,remote,digest("destination")) {fs::create_directories(root/"local");fs::create_directories(root/"journal");fs::create_directories(root/"remote");}
  ~Fixture(){fs::remove_all(root);}
  void file(const std::string& p,const std::string& s="synthetic QA export\n"){fs::create_directories((local.root/p).parent_path());std::ofstream f(local.root/p,std::ios::binary);f<<s;}
  pro::Step run(const std::string& p="test.txt",uint64_t now=2000000000){return backup.process(p,now);}
};
struct HttpRemote:pro::Remote {
  Local& l;std::string base;fs::path root;int putCount=0;
  HttpRemote(Local& l_,std::string b,fs::path r):l(l_),base(b),root(r){}
  pro::Result request(const std::string& p,bool put) {
    std::vector<std::string> args={"--silent","--show-error","--max-time","3","--user","mock:pass","--output",(root/"body").string(),"--write-out","%{http_code}","--url",base+pro::encodePath(p)};
    if(put){args.push_back("--upload-file");args.push_back((l.root/p).string());++putCount;}
    // Redirect only child's stdout to a private test status file, with no shell.
    std::cout.flush();std::cerr.flush();pid_t pid=fork();if(pid==0){FILE* f=freopen((root/"status").c_str(),"wb",stdout);if(!f)_exit(125);std::vector<char*> av={const_cast<char*>("curl")};for(auto& s:args)av.push_back(const_cast<char*>(s.c_str()));av.push_back(nullptr);execvp("curl",av.data());_exit(127);}
    int status;waitpid(pid,&status,0);if(!WIFEXITED(status)||WEXITSTATUS(status))return {false,-1};
    std::ifstream f(root/"status");int code=0;f>>code;return {code==200||code==201||code==204,code};
  }
  pro::Result put(const std::string& p,uint64_t)override{return request(p,true);}
  pro::Result verify(const std::string& p,const std::string& h,uint64_t size)override {
    auto r=request(p,false);if(!r.ok)return r;Local downloaded(root);std::string actual;uint64_t n;if(!downloaded.fingerprint("body",actual,n))return {false,-1};return {h==actual&&n==size,h==actual&&n==size?200:pro::DigestMismatch};
  }
};
struct TestDisk {
 std::vector<uint8_t> data;unsigned reads=0,writes=0,failRead=0,failWrite=0;
 TestDisk():data(32768){for(size_t i=0;i<data.size();++i)data[i]=i%251;}
 bool read(uint8_t* out,uint32_t lba,unsigned count){++reads;if(reads==failRead)return false;REQUIRE(uint64_t(lba+count)*512<=data.size());memcpy(out,data.data()+lba*512,count*512);return true;}
 bool write(const uint8_t* in,uint32_t lba,unsigned count){++writes;if(writes==failWrite)return false;REQUIRE(uint64_t(lba+count)*512<=data.size());memcpy(data.data()+lba*512,in,count*512);return true;}
};
int main(int argc,char** argv) {
  std::vector<std::pair<std::string,std::function<void()>>> tests;
  auto test=[&](std::string name,std::function<void()> f){tests.emplace_back(name,f);};
  test("USB block batching preserves partial sectors and read contents",[]{
    uint8_t bounce[2048];
    for(unsigned offset:{0U,1U,73U,511U})for(unsigned length:{1U,511U,512U,513U,2048U,4097U,8192U}){
      TestDisk disk;auto expected=disk.data;std::vector<uint8_t> bytes(length,0xa7);std::fill(expected.begin()+1024+offset,expected.begin()+1024+offset+length,0xa7);
      REQUIRE(pro::transferBlocks(disk,2,offset,bytes.data(),length,true,bounce,sizeof bounce));REQUIRE(disk.data==expected);
      std::fill(bytes.begin(),bytes.end(),0);REQUIRE(pro::transferBlocks(disk,2,offset,bytes.data(),length,false,bounce,sizeof bounce));REQUIRE(std::all_of(bytes.begin(),bytes.end(),[](uint8_t n){return n==0xa7;}));
    }
  });
  test("aligned USB spans use fewer bounded SD commands",[]{
    TestDisk disk;uint8_t bounce[2048];std::vector<uint8_t> bytes(8192,0x42);
    REQUIRE(pro::transferBlocks(disk,0,0,bytes.data(),bytes.size(),true,bounce,sizeof bounce));REQUIRE(disk.writes==4);REQUIRE(disk.reads==0);
    REQUIRE(!pro::transferBlocks(disk,0,512,bytes.data(),512,true,bounce,sizeof bounce));REQUIRE(disk.writes==4);
  });
  test("USB block errors stop further writes and preserve untouched sectors",[]{
    TestDisk disk;auto original=disk.data;uint8_t bounce[2048];std::vector<uint8_t> bytes(8192,0x42);disk.failRead=1;
    REQUIRE(!pro::transferBlocks(disk,0,1,bytes.data(),100,true,bounce,sizeof bounce));REQUIRE(disk.data==original);REQUIRE(disk.writes==0);
    disk.failWrite=2;REQUIRE(!pro::transferBlocks(disk,0,0,bytes.data(),bytes.size(),true,bounce,sizeof bounce));REQUIRE(disk.writes==2);
    REQUIRE(std::equal(original.begin()+2048,original.end(),disk.data.begin()+2048));
  });
  test("verification and retry timestamps use transfer completion time",[]{
    Fixture f;f.file("QA/clock.pdf");pro::Backup engine(f.local,f.journal,f.remote,digest("destination"),[]()->uint64_t{return 2000000900;});
    auto result=engine.process("QA/clock.pdf",2000000000);REQUIRE(result.outcome==pro::Outcome::Verified);REQUIRE(result.verifiedAt==2000000900);
    f.file("QA/failure.pdf");f.remote.putError=503;REQUIRE(engine.process("QA/failure.pdf",2000000000).outcome==pro::Outcome::Failed);
    pro::Record record;REQUIRE(f.journal.load(digest("QA/failure.pdf"),record)==pro::Load::Found);REQUIRE(record.next>2000000900);
  });
  test("LAN dashboard permits observation but cannot change configuration",[]{
    for(const auto& path:{"/","/status","/app.js","/diagnostics","/favicon.ico"})REQUIRE(pro::dashboardRoute("GET",path));
    REQUIRE(pro::dashboardRoute("POST","/history"));
    for(const auto& path:{"/config","/resume","/wifi-connect","/wifi-disconnect","/wifi-scan","/cloud-test","/cloud-check","/presets"})for(const auto& method:{"GET","POST"})REQUIRE(!pro::dashboardRoute(method,path));
    REQUIRE(!pro::dashboardRoute("POST","/status"));REQUIRE(!pro::dashboardRoute("GET","/history?cursor=foo"));
  });
  test("transfer meter handles bounds restart and clock rollover",[]{
    pro::TransferMeter m;m.start(10000,UINT32_MAX-200);m.update(1000,299);REQUIRE(m.bytesPerSecond==2000);REQUIRE(m.percent()==10);
    m.update(500,300);REQUIRE(m.bytesPerSecond==0);m.update(20000,800);REQUIRE(m.done==10000);REQUIRE(m.percent()==100);
    m.start(0,0);m.update(0,500);REQUIRE(m.percent()==100);REQUIRE(m.bytesPerSecond==0);
  });
  test("LCD distinguishes transfer verification retry and completion",[]{
    pro::DisplayInput s;s.wifi=true;s.active=true;s.phase=pro::TransferPhase::Uploading;s.total=8388608;s.done=4194304;s.percent=50;s.speed=65536;
    auto f=pro::displayFrame(s);REQUIRE(std::string(f.title)=="UPLOADING");REQUIRE(f.percent==50);REQUIRE(std::string(f.detail)=="4.0 / 8.0 MB");
    s.phase=pro::TransferPhase::Checking;REQUIRE(std::string(pro::displayFrame(s).title)=="CHECKING");s.active=false;s.error=412;REQUIRE(std::string(pro::displayFrame(s).title)=="RETRYING");
    s.error=-2107;REQUIRE(std::string(pro::displayFrame(s).title)=="CHECK FILES");
    s.error=0;s.complete=true;s.verified=3;REQUIRE(std::string(pro::displayFrame(s).title)=="BACKED UP");s.wifi=false;REQUIRE(std::string(pro::displayFrame(s).title)=="WI-FI OFF");
  });
  test("history enumerates latest receipts once in cursor order",[]{
    Fixture f;for(const auto& path:{"QA/a.pdf","QA/b.pdf","QA/c.pdf"}){f.file(path);REQUIRE(f.run(path).outcome==pro::Outcome::Verified);}
    std::string cursor,next;pro::Record record;unsigned count=0;
    while(f.journal.next(cursor,next,record)==pro::Load::Found){REQUIRE(next>cursor);REQUIRE(record.state==pro::State::Verified);REQUIRE(record.verifiedAt!=0);cursor=next;++count;}
    REQUIRE(count==3);REQUIRE(f.journal.next("../",next,record)==pro::Load::IOError);
    REQUIRE(f.journal.next("",next,record)==pro::Load::Found);std::ofstream(f.root/"journal"/(next+".1"),std::ios::trunc)<<"torn";
    REQUIRE(f.journal.next("",next,record)==pro::Load::Found);
  });
  test("ownership eject is one-way",[]{pro::Ownership o;REQUIRE(o.hostIO());REQUIRE(!o.mounted(true));REQUIRE(o.eject());REQUIRE(!o.hostIO());REQUIRE(o.mounted(true));REQUIRE(!o.eject());REQUIRE(!o.hostIO());});
  test("disconnect and mount failure fail closed",[]{pro::Ownership o;o.fault();REQUIRE(!o.hostIO());REQUIRE(!o.eject());pro::Ownership b;REQUIRE(b.eject());REQUIRE(!b.mounted(false));REQUIRE(b.state()==pro::Owner::Fault);});
  test("phone/reset reboot waits for released idle storage",[]{pro::Ownership o;REQUIRE(!pro::canRestart(o.state(),false));REQUIRE(o.eject());REQUIRE(pro::canRestart(o.state(),false));REQUIRE(o.mounted(true));REQUIRE(!pro::canRestart(o.state(),true));REQUIRE(pro::canRestart(o.state(),false));o.fault();REQUIRE(!pro::canRestart(o.state(),true));REQUIRE(pro::canRestart(o.state(),false));});
  test("sector range overflow and partial bounds",[]{REQUIRE(pro::validRange(10,9,511,1));REQUIRE(!pro::validRange(10,9,511,2));REQUIRE(!pro::validRange(10,10,0,1));REQUIRE(!pro::validRange(10,0,512,1));REQUIRE(!pro::validRange(10,0,0,0));REQUIRE(!pro::validRange(10,UINT32_MAX,0,UINT32_MAX));});
  test("path traversal and UTF8 validation",[]{for(const auto& p:{"../a","a/../b","/a","a//b","a/","a\\b",".cloud-stick/config.json","a/./b","a:b"})REQUIRE(!pro::validPath(p));REQUIRE(pro::validPath("folder/QA-é-测试.txt"));REQUIRE(!pro::validPath(std::string("\xC0\xAF")));REQUIRE(!pro::validPath(std::string("\xED\xA0\x80")));REQUIRE(!pro::validPath(std::string("a\0b",3)));REQUIRE(!pro::validPath(std::string(241,'a')));});
  test("unsigned Unicode escaping",[]{REQUIRE(pro::encodePath("a/é #?.txt")=="a/%C3%A9%20%23%3F.txt");});
  test("HTTPS endpoint validation",[]{REQUIRE(pro::validURL("https://backup.example:8443/dav/"));for(const auto& s:{"http://host/dav/","https://user:pass@host/dav/","https://host/\r\nx","https:///","https://host:0/","https://host:65536/","https://host/path?x"})REQUIRE(!pro::validURL(s));});
  test("WiFi validation requires an explicit open or secured mode",[]{
    REQUIRE(pro::validWiFi("Guest Network","",true));
    REQUIRE(!pro::validWiFi("Guest Network","",false));
    REQUIRE(!pro::validWiFi("Guest Network","password",true));
    REQUIRE(pro::validWiFi("Home WiFi","12345678",false));
    REQUIRE(pro::validWiFi("Home WiFi",std::string(63,'p'),false));
    REQUIRE(!pro::validWiFi("Home WiFi",std::string(7,'p'),false));
    REQUIRE(!pro::validWiFi("Home WiFi",std::string(64,'p'),false));
    REQUIRE(pro::validWiFi("  Home WiFi  ","password",false));
  });
  test("WiFi SSID bounds count bytes and reject controls",[]{
    std::string unicode;for(int i=0;i<16;++i)unicode+="é";
    REQUIRE(unicode.size()==32);REQUIRE(pro::validWiFi(unicode,"",true));
    REQUIRE(!pro::validWiFi(unicode+"a","",true));
    REQUIRE(pro::validWiFi(std::string(32,'s'),"password",false));
    REQUIRE(!pro::validWiFi(std::string(33,'s'),"password",false));
    REQUIRE(!pro::validWiFi("","",true));
    for(const auto& ssid:std::vector<std::string>{"Guest\nWiFi","Guest\tWiFi","Guest\rWiFi",std::string("Guest\0WiFi",10),std::string("Guest")+char(127)}) {
      REQUIRE(!pro::validWiFi(ssid,"",true));REQUIRE(!pro::validWiFi(ssid,"password",false));
    }
  });
  test("WiFi passwords reject controls without rejecting ordinary spaces",[]{
    REQUIRE(pro::validWiFi("Home","normal spaced password",false));
    for(const auto& password:std::vector<std::string>{"bad\npass!","bad\tpass!",std::string("sec\0ret88",9),std::string(1,char(127))+"1234567"})REQUIRE(!pro::validWiFi("Home",password,false));
  });
  test("record codec and corruption",[]{pro::Record r;r.path="nested/a.txt";r.digest=digest("a");r.destination=digest("d");r.size=UINT64_C(5000000000);r.error=-9;r.sequence=12;uint8_t bytes[pro::RecordSize];REQUIRE(pro::encodeRecord(r,bytes));pro::Record out;REQUIRE(pro::decodeRecord(bytes,out));REQUIRE(out.size==r.size&&out.error==-9&&out.path==r.path);for(size_t i=0;i<sizeof bytes;++i){bytes[i]^=1;REQUIRE(!pro::decodeRecord(bytes,out));bytes[i]^=1;}});
  test("capped exponential backoff",[]{REQUIRE(pro::retrySeconds(1)==10);REQUIRE(pro::retrySeconds(2)==20);REQUIRE(pro::retrySeconds(10)==3600);REQUIRE(pro::retrySeconds(UINT32_MAX)==3600);});
  test("portal bounded GET and JSON POST",[]{pro::Request r;REQUIRE(pro::parseRequest("GET /status HTTP/1.1\r\nHost: 192.168.4.1\r\nAuthorization: Basic x\r\n\r\n",r));REQUIRE(r.authorization=="Basic x");REQUIRE(pro::parseRequest("POST /config HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\nContent-Length: 6144\r\n\r\n",r));REQUIRE(r.length==6144);});
  test("setup discovery GET routes are exact and bounded",[]{
    for(const auto& path:{"/wifi-scan","/wifi-connect","/presets","/favicon.ico"}) {
      pro::Request r;
      REQUIRE(pro::parseRequest(std::string("GET ")+path+" HTTP/1.1\r\nHost: 192.168.4.1\r\nAuthorization: Basic setup\r\n\r\n",r));
      REQUIRE(r.method=="GET"&&r.path==path&&r.length==0&&r.authorization=="Basic setup");
    }
  });
  test("WiFi scan POST preserves auth CSRF and JSON framing",[]{
    pro::Request r;
    REQUIRE(pro::parseRequest("POST /wifi-scan HTTP/1.1\r\nHost: 192.168.4.1:80\r\nAuthorization: Basic setup\r\nX-CSRF-Token: scan-token\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n",r));
    REQUIRE(r.method=="POST"&&r.path=="/wifi-scan"&&r.length==2);
    REQUIRE(r.authorization=="Basic setup"&&r.csrf=="scan-token");
  });
  test("WiFi connection routes keep protected JSON framing",[]{
    for(const auto& path:{"/wifi-connect","/wifi-disconnect"}) {
      pro::Request r;
      REQUIRE(pro::parseRequest(std::string("POST ")+path+" HTTP/1.1\r\nHost: 192.168.4.1\r\nAuthorization: Basic setup\r\nX-CSRF-Token: wifi-token\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n",r));
      REQUIRE(r.method=="POST"&&r.path==path&&r.length==2&&r.csrf=="wifi-token");
      REQUIRE(!pro::parseRequest(std::string("POST ")+path+" HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n",r));
    }
  });
  test("setup discovery rejects aliases queries and traversal",[]{
    for(const auto& path:{"/wifi-scan/","/wifi-scan?refresh=1","/wifi-scan/../config","/%77ifi-scan","//wifi-scan","/WIFI-SCAN","/wifi-connect?next=http://example.invalid/","/wifi-disconnect/../config","/presets/","/presets?x","/presets/../config","/favicon.ico/../config","http://192.168.4.1/presets"}) {
      pro::Request r;
      REQUIRE(!pro::parseRequest(std::string("GET ")+path+" HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",r));
    }
  });
  test("Android Chrome setup headers fit the existing bounded parser",[]{
    const std::string header="GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: keep-alive\r\nCache-Control: max-age=0\r\nAuthorization: Basic YWRtaW46c3ludGhldGlj\r\nUpgrade-Insecure-Requests: 1\r\nUser-Agent: Mozilla/5.0 (Linux; Android 16; Pixel 9) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Mobile Safari/537.36\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8\r\nAccept-Encoding: gzip, deflate\r\nAccept-Language: en-US,en;q=0.9\r\nReferer: http://192.168.4.1/\r\n\r\n";
    REQUIRE(header.size()<pro::HeaderLimit);pro::Request r;const char* reason="old error";
    REQUIRE(pro::parseRequest(header,r,&reason));REQUIRE(reason==nullptr);REQUIRE(r.path=="/");
  });
  test("portal request failures explain the boundary without reflecting private input",[]{
    const std::string secret="never-reflect-this-token";
    pro::Request r;const char* reason=nullptr;
    REQUIRE(!pro::parseRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("Incomplete")!=std::string::npos);
    REQUIRE(!pro::parseRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nX-Pad: "+std::string(pro::HeaderLimit,'x')+"\r\n\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("2048")!=std::string::npos);
    REQUIRE(!pro::parseRequest(std::string(pro::HeaderLimit,'x'),r,&reason));
    REQUIRE(reason&&std::string(reason).find("2048")!=std::string::npos);
    REQUIRE(!pro::parseRequest("GET /"+secret+" HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("Unknown")!=std::string::npos&&std::string(reason).find(secret)==std::string::npos);
    REQUIRE(!pro::parseRequest("GET / HTTP/1.1\r\nHost: "+secret+"\r\n\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("setup address")!=std::string::npos&&std::string(reason).find(secret)==std::string::npos);
    REQUIRE(!pro::parseRequest("POST /wifi-connect HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\nContent-Length: 6145\r\n\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("6144")!=std::string::npos);
    REQUIRE(!pro::parseRequest("POST /wifi-connect HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\n\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("Content-Length")!=std::string::npos);
    REQUIRE(!pro::parseRequest("POST /wifi-connect HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\nAuthorization: "+secret+"\r\nAuthorization: "+secret+"\r\n\r\n",r,&reason));
    REQUIRE(reason&&std::string(reason).find("Duplicate Authorization")!=std::string::npos&&std::string(reason).find(secret)==std::string::npos);
  });
  test("WiFi scan keeps strict POST request boundaries",[]{
    for(const auto& headers:{
      "Content-Type: application/json\r\n",
      "Content-Length: 2\r\n",
      "Content-Type: text/plain\r\nContent-Length: 2\r\n",
      "Content-Type: application/json\r\nContent-Length: 1\r\n",
      "Content-Type: application/json\r\nContent-Length: 6145\r\n",
      "Content-Type: application/json\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n",
      "Content-Type: application/json\r\nContent-Length: 2\r\nX-CSRF-Token: a\r\nX-CSRF-Token: b\r\n"}) {
      pro::Request r;
      REQUIRE(!pro::parseRequest(std::string("POST /wifi-scan HTTP/1.1\r\nHost: 192.168.4.1\r\n")+headers+"\r\n",r));
    }
    pro::Request r;
    REQUIRE(!pro::parseRequest("GET /presets HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 2\r\n\r\n",r));
  });
  test("portal accepts only the active setup Host after subnet migration",[]{
    for(const auto& host:{"172.31.254.1","10.253.254.1"}){
      pro::Request r;const char* reason=nullptr;
      REQUIRE(pro::parseRequest(std::string("GET /status HTTP/1.1\r\nHost: ")+host+"\r\n\r\n",r,&reason,host));
      REQUIRE(pro::parseRequest(std::string("GET /status HTTP/1.1\r\nHost: ")+host+":80\r\n\r\n",r,&reason,host));
      REQUIRE(!pro::parseRequest("GET /status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",r,&reason,host));
      REQUIRE(!pro::parseRequest(std::string("GET /status HTTP/1.1\r\nHost: ")+host+".attacker.example\r\n\r\n",r,&reason,host));
      REQUIRE(!pro::parseRequest(std::string("GET /status HTTP/1.1\r\nHost: ")+host+"\r\nHost: "+host+"\r\n\r\n",r,&reason,host));
    }
  });
  test("portal rejects smuggling and oversized bodies",[]{pro::Request r;for(const auto& h:{"Content-Length: 6145\r\n","Content-Length: -1\r\n","Content-Length: 2\r\nContent-Length: 2\r\n","Content-Length: 2\r\nTransfer-Encoding: chunked\r\n","Content-Length: 99999999999999999999\r\n"})REQUIRE(!pro::parseRequest(std::string("POST /config HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\n")+h+"\r\n",r));});
  test("portal rejects foreign hosts and multipart",[]{pro::Request r;REQUIRE(!pro::parseRequest("GET /status HTTP/1.1\r\nHost: attacker.example\r\n\r\n",r));REQUIRE(!pro::parseRequest("POST /config HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: multipart/form-data\r\nContent-Length: 2\r\n\r\n",r));REQUIRE(!pro::parseRequest("GET /status HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 2\r\n\r\n",r));});
  test("portal header cap and incomplete headers",[]{pro::Request r;REQUIRE(!pro::parseRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n",r));REQUIRE(!pro::parseRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nX-Pad: "+std::string(2048,'x')+"\r\n\r\n",r));REQUIRE(!pro::parseRequest("GET /status HTTP/1.1\r\nHost: 192.168.4.1\r\nAuthorization: x\r\nAuthorization: y\r\n\r\n",r));});
  test("portal comparison and deterministic parser fuzz",[]{REQUIRE(pro::constantEqual("token","token"));REQUIRE(!pro::constantEqual("token","tokeN"));REQUIRE(!pro::constantEqual("token",""));uint32_t seed=1;for(int i=0;i<10000;++i){std::string s;for(int j=0;j<(i%300);++j){seed=seed*1664525+1013904223;s+=char(seed>>24);}pro::Request r;REQUIRE(!pro::parseRequest(s,r));}});
  test("small upload verified and local retained",[]{Fixture f;f.file("test.txt");REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(fs::exists(f.local.root/"test.txt"));REQUIRE(f.remote.puts==1&&f.remote.gets==1);});
  test("zero byte file",[]{Fixture f;f.file("test.txt","");REQUIRE(f.run().outcome==pro::Outcome::Verified);});
  test("large streamed file bounded chunk",[]{Fixture f;f.file("test.txt","");std::ofstream out(f.local.root/"test.txt",std::ios::binary);std::string block(2048,'Q');for(int i=0;i<8192;++i)out<<block;out.close();REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.local.maxChunk<=2048);});
  test("nested folders and duplicate filenames",[]{Fixture f;for(const auto& p:{"a/same.txt","b/same.txt","x/y/z/a.txt"}){f.file(p,p);REQUIRE(f.run(p).outcome==pro::Outcome::Verified);}REQUIRE(f.remote.puts==3);});
  test("same-size content modification",[]{Fixture f;f.file("test.txt","AAAA");REQUIRE(f.run().outcome==pro::Outcome::Verified);f.file("test.txt","BBBB");REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==2);});
  test("unchanged file avoids duplicate upload after reboot",[]{Fixture f;f.file("test.txt");REQUIRE(f.run().outcome==pro::Outcome::Verified);pro::Backup reboot(f.local,f.journal,f.remote,digest("destination"));REQUIRE(reboot.process("test.txt",2000000010).outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==1);});
  test("destination change invalidates success",[]{Fixture f;f.file("test.txt");f.run();pro::Backup other(f.local,f.journal,f.remote,digest("other destination"));REQUIRE(other.process("test.txt",2000000010).outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==2);});
  test("failed authentication retained and backoff persisted",[]{Fixture f;f.file("test.txt");f.remote.putError=401;REQUIRE(f.run().error==401);pro::Record r;REQUIRE(f.journal.load(f.local.key("test.txt"),r)==pro::Load::Found);REQUIRE(r.error==401&&r.attempts==1);REQUIRE(f.run("test.txt",2000000001).outcome==pro::Outcome::Pending);REQUIRE(f.remote.puts==1);f.remote.putError=0;REQUIRE(f.run("test.txt",2000000010).outcome==pro::Outcome::Verified);});
  test("interrupted upload then reconnect",[]{Fixture f;f.file("test.txt");f.remote.putError=-1;REQUIRE(f.run().outcome==pro::Outcome::Failed);f.remote.putError=0;REQUIRE(f.run("test.txt",2000000010).outcome==pro::Outcome::Verified);});
  test("server downtime retries with increasing delay",[]{Fixture f;f.file("test.txt");f.remote.putError=503;f.run();f.run("test.txt",2000000010);REQUIRE(f.remote.puts==2);f.run("test.txt",2000000020);REQUIRE(f.remote.puts==2);f.remote.putError=0;REQUIRE(f.run("test.txt",2000000030).outcome==pro::Outcome::Verified);});
  test("transferred is not verified and reboot retries GET",[]{Fixture f;f.file("test.txt");f.remote.getError=-1;REQUIRE(f.run().outcome==pro::Outcome::Failed);pro::Record r;f.journal.load(f.local.key("test.txt"),r);REQUIRE(r.state==pro::State::Transferred);pro::Backup reboot(f.local,f.journal,f.remote,digest("destination"));f.remote.getError=0;REQUIRE(reboot.process("test.txt",2000000010).outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==1);});
  test("hash mismatch never verified; reupload repairs",[]{Fixture f;f.file("test.txt");f.remote.corrupt=true;REQUIRE(f.run().error==pro::DigestMismatch);pro::Record r;f.journal.load(f.local.key("test.txt"),r);REQUIRE(r.state==pro::State::Pending);f.remote.corrupt=false;REQUIRE(f.run("test.txt",2000000010).outcome==pro::Outcome::Verified);});
  test("remote disappearance after PUT requeues upload",[]{Fixture f;f.file("test.txt");f.remote.getError=503;REQUIRE(f.run().outcome==pro::Outcome::Failed);fs::remove(f.remote.cloud.root/"test.txt");f.remote.getError=0;REQUIRE(f.run("test.txt",2000000010).error==404);REQUIRE(f.remote.puts==1);REQUIRE(f.run("test.txt",2000000030).outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==2);});
  test("metadata full fails before network",[]{Fixture f;f.file("test.txt");f.journal.fail=true;REQUIRE(f.run().outcome==pro::Outcome::MetadataError);REQUIRE(f.remote.puts==0);});
  test("power loss after PUT before receipt save",[]{Fixture f;f.file("test.txt");f.journal.failAt=2;REQUIRE(f.run().outcome==pro::Outcome::MetadataError);REQUIRE(f.remote.puts==1);f.journal.failAt=0;REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==2);});
  test("power loss after receipt before verified commit",[]{Fixture f;f.file("test.txt");f.journal.failAt=3;REQUIRE(f.run().outcome==pro::Outcome::MetadataError);f.journal.failAt=0;REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==1);});
  test("corrupt both metadata generations requeues safely",[]{Fixture f;f.file("test.txt");f.run();for(const auto& e:fs::directory_iterator(f.root/"journal")){std::ofstream o(e.path(),std::ios::binary|std::ios::trunc);o<<"corrupt";}auto s=f.run();REQUIRE(s.repaired);REQUIRE(s.outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==2);});
  test("torn newest generation recovers older state",[]{Fixture f;f.file("test.txt");f.run();std::ofstream out(f.root/"journal"/(pro::journalStem(f.local.key("test.txt"))+".1"),std::ios::binary|std::ios::trunc);out<<"torn";out.close();REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==1);});
  test("orphan temp is ignored",[]{Fixture f;f.file("test.txt");std::ofstream out(f.root/"journal/pending.tmp");out<<"torn";out.close();REQUIRE(f.run().outcome==pro::Outcome::Verified);});
  test("journal quota bounds persistent identities",[]{Fixture f;for(size_t i=0;i<pro::FileLimit;++i){auto p=std::to_string(i)+".txt";f.file(p);REQUIRE(f.run(p).outcome==pro::Outcome::Verified);}f.file("overflow.txt");REQUIRE(f.run("overflow.txt").outcome==pro::Outcome::MetadataError);REQUIRE(f.remote.puts==int(pro::FileLimit));});
  test("SD read error never marks success",[]{Fixture f;f.file("test.txt");f.local.broken=true;REQUIRE(f.run().outcome==pro::Outcome::LocalError);REQUIRE(f.remote.puts==0);});
  test("full SD source requires no local writes",[]{Fixture f;f.file("test.txt");fs::permissions(f.local.root/"test.txt",fs::perms::owner_read);REQUIRE(f.run().outcome==pro::Outcome::Verified);});
  test("clock rollback is bounded",[]{Fixture f;f.file("test.txt");f.remote.putError=503;f.run();f.remote.putError=0;REQUIRE(f.run("test.txt",1900000000).outcome==pro::Outcome::Verified);});
  test("journal filenames fit LittleFS without truncating the SHA256 key",[]{
    REQUIRE(pro::journalStem(std::string(64,'0'))==std::string(52,'a'));
    REQUIRE(pro::journalStem(std::string(64,'f'))==std::string(51,'7')+"q");
    REQUIRE(pro::journalStem("invalid").empty());
    auto stem=pro::journalStem(digest("name"));REQUIRE(stem.size()+2<64);
    std::string key(64,'0');for(size_t i=0;i<64;++i){key[i]='1';REQUIRE(pro::journalStem(key)!=std::string(52,'a'));key[i]='0';}
  });
  test("legacy receipts survive migration on filesystems accepting long names",[]{
    Fixture f;f.file("test.txt");REQUIRE(f.run().outcome==pro::Outcome::Verified);
    auto key=f.local.key("test.txt"),stem=pro::journalStem(key);
    for(const auto* suffix:{".0",".1"})fs::rename(f.root/"journal"/(stem+suffix),f.root/"journal"/(key+suffix));
    REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==1);
    f.file("test.txt","changed content");REQUIRE(f.run().outcome==pro::Outcome::Verified);REQUIRE(f.remote.puts==2);
    REQUIRE(fs::exists(f.root/"journal"/(stem+".1"))||fs::exists(f.root/"journal"/(stem+".0")));
  });
  test("USB diagnostic commands reject oversized and embedded control bytes",[]{
    pro::DiagnosticLine line;
    auto feed=[&](std::string s){auto result=pro::DiagnosticCommand::None;for(char c:s)result=line.push(c);return result;};
    REQUIRE(feed("STATUS\r\n")==pro::DiagnosticCommand::Status);
    REQUIRE(feed(std::string(10000,'X')+"TEST\n")==pro::DiagnosticCommand::Invalid);
    REQUIRE(feed(std::string("TEST\0suffix\n",12))==pro::DiagnosticCommand::Invalid);
    REQUIRE(feed("TEST\n")==pro::DiagnosticCommand::Test);
    REQUIRE(feed("LOG\n")==pro::DiagnosticCommand::Log);
    REQUIRE(feed("CHECK\n")==pro::DiagnosticCommand::Check);
    REQUIRE(feed("SETUP\n")==pro::DiagnosticCommand::Setup);
    REQUIRE(feed("ERASE\n")==pro::DiagnosticCommand::Invalid);
  });
  test("diagnostic ring retains only newest bounded events in chronological order",[]{
    pro::DiagnosticRing ring;REQUIRE(ring.size()==0);REQUIRE(ring.at(99).kind==0);
    for(unsigned i=0;i<10000;++i)ring.add({1,i,2000000000ULL+i,7,int32_t(i)});
    REQUIRE(ring.size()==16);for(unsigned i=0;i<16;++i)REQUIRE(ring.at(i).code==int32_t(9984+i));
  });
  test("only explicit safe software restart retains backup ownership",[]{
    REQUIRE(pro::resumeBackupBoot(true,0x43535042));
    REQUIRE(!pro::resumeBackupBoot(false,0x43535042));
    REQUIRE(!pro::resumeBackupBoot(true,0));
    for(uint32_t c=0;c<100000;++c)REQUIRE(!pro::resumeBackupBoot(true,c));
  });
  test("diagnostic routes retain exact hosts framing and no query tokens",[]{
    pro::Request r;for(const auto* path:{"/diagnostics","/cloud-check","/cloud-test"}){
      REQUIRE(pro::parseRequest(std::string("GET ")+path+" HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",r));
      REQUIRE(!pro::parseRequest(std::string("GET ")+path+"?token=secret HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",r));
      REQUIRE(!pro::parseRequest(std::string("POST ")+path+" HTTP/1.1\r\nHost: evil.example\r\nContent-Length: 2\r\nContent-Type: application/json\r\n\r\n",r));
    }
  });

  test("USB write invalidates active reader immediately and a fresh quiet session is required",[]{
    pro::SnapshotGuard g;REQUIRE(!g.begin(119999,120000));REQUIRE(g.begin(120000,120000));REQUIRE(g.valid());
    g.wrote(120001);REQUIRE(!g.valid());REQUIRE(!g.begin(300000,120000));g.end();
    REQUIRE(!g.begin(240000,120000));REQUIRE(g.begin(240001,120000));REQUIRE(g.valid());
    g.invalidate();REQUIRE(!g.valid());g.end();REQUIRE(g.begin(240001,0));REQUIRE(g.valid());
  });
  test("idle timing survives millis rollover and subsequent writes extend the wait",[]{
    pro::SnapshotGuard g;g.wrote(UINT32_MAX-1000);REQUIRE(g.remaining(999,3000)==1000);
    g.wrote(999);REQUIRE(g.remaining(1999,3000)==2000);REQUIRE(!g.begin(3998,3000));REQUIRE(g.begin(3999,3000));
  });
  test("overnight windows include start exclude end and can cross midnight",[]{
    for(unsigned h=0;h<24;++h){REQUIRE(pro::inBackupWindow(h,1,5)==(h>=1&&h<5));REQUIRE(pro::inBackupWindow(h,22,5)==(h>=22||h<5));REQUIRE(pro::inBackupWindow(h,5,5));}
    REQUIRE(!pro::inBackupWindow(24,1,5));REQUIRE(!pro::inBackupWindow(1,24,5));
  });
  test("copy interruption during PUT or readback does not create a failure or retry penalty",[]{
    for(bool duringGet:{false,true}){Fixture f;f.file("test.txt");
      if(duringGet)f.remote.getError=pro::SnapshotChanged;else f.remote.putError=pro::SnapshotChanged;
      auto step=f.run();REQUIRE(step.outcome!=pro::Outcome::Verified);REQUIRE(step.error==pro::SnapshotChanged);
      pro::Record receipt;REQUIRE(f.journal.load(f.local.key("test.txt"),receipt)==pro::Load::Found);
      REQUIRE(receipt.attempts==0);REQUIRE(receipt.next==0);REQUIRE(receipt.verifiedAt==0);
      f.remote.getError=0;f.remote.putError=0;REQUIRE(f.run("test.txt",2000000001).outcome==pro::Outcome::Verified);
    }
  });
  test("staging namespace cannot collide with a source file",[]{
    REQUIRE(!pro::validPath("._csp_uploads_1234/file.part"));REQUIRE(!pro::validPath("._csp_uploads_"));REQUIRE(pro::validPath("folder/._csp_uploads_test"));
  });
  test("DAV replacement accepts only a single ordinary file with a strong validator",[]{
    std::string etag;
    auto xml=[](std::string props,std::string status="200 OK"){return "<D:multistatus xmlns:D=\"DAV:\"><D:response><D:propstat><D:prop>"+props+"</D:prop><D:status>HTTP/1.1 "+status+"</D:status></D:propstat></D:response></D:multistatus>";};
    std::string valid="<D:resourcetype/><D:getetag>&quot;1234&quot;</D:getetag>";
    REQUIRE(pro::fileProperties(xml(valid),etag));REQUIRE(etag=="\"1234\"");
    REQUIRE(!pro::fileProperties(xml("<D:resourcetype><D:collection/></D:resourcetype><D:getetag>&quot;1234&quot;</D:getetag>"),etag));
    REQUIRE(!pro::fileProperties(xml(valid,"404 Not Found"),etag));
    REQUIRE(!pro::fileProperties(xml(valid+valid),etag));
    REQUIRE(!pro::fileProperties(xml("<D:resourcetype/><D:getetag>W/&quot;weak&quot;</D:getetag>"),etag));
    REQUIRE(!pro::fileProperties(xml("<D:resourcetype/><D:getetag>&quot;bad&#10;tag&quot;</D:getetag>"),etag));
    auto foreign=xml(valid);foreign.replace(foreign.find("DAV:"),4,"evil");REQUIRE(!pro::fileProperties(foreign,etag));
    REQUIRE(!pro::fileProperties(std::string(4097,'x'),etag));REQUIRE(!pro::fileProperties("<!DOCTYPE foo>"+xml(valid),etag));
    REQUIRE(!pro::fileProperties("<D:multistatus xmlns:D=\"DAV:\"><D:response/><D:response/></D:multistatus>",etag));
  });
  if(argc==2) {
    const std::string base=argv[1];
    test("mock HTTP small nested Unicode and 16MiB",[base]{Fixture f;HttpRemote http(f.local,base,f.root);pro::Backup b(f.local,f.journal,http,digest(base));for(const auto& p:{"tiny.txt","nested/same.txt","other/same.txt","QA-é.txt","empty.txt"}){f.file(p,p==std::string("empty.txt")?"":"synthetic");REQUIRE(b.process(p,2000000000).outcome==pro::Outcome::Verified);}f.file("large.bin","");std::ofstream large(f.local.root/"large.bin",std::ios::binary);std::string block(2048,'X');for(int i=0;i<8192;++i)large<<block;large.close();REQUIRE(b.process("large.bin",2000000000).outcome==pro::Outcome::Verified);REQUIRE(http.putCount==6);});
    test("mock HTTP 401 503 disconnect corrupt readback recovery",[base]{for(const auto& p:{"auth.txt","down.txt","disconnect.txt","disconnect-large.bin","corrupt.txt"}){Fixture f;HttpRemote http(f.local,base,f.root);pro::Backup b(f.local,f.journal,http,digest(base));f.file(p);if(p==std::string("disconnect-large.bin")){std::ofstream large(f.local.root/p,std::ios::binary);std::string block(2048,'I');for(int i=0;i<8192;++i)large<<block;}REQUIRE(b.process(p,2000000000).outcome==pro::Outcome::Failed);REQUIRE(b.process(p,2000000010).outcome==pro::Outcome::Verified);}});
  }
  unsigned failed=0;for(auto& t:tests){try{t.second();std::cout<<"PASS "<<t.first<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<t.first<<": "<<e.what()<<'\n';}}
  std::cout<<tests.size()-failed<<"/"<<tests.size()<<" passed\n";return failed?1:0;
}
