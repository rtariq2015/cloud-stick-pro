// Run the production journal through a 64-byte filename boundary, matching the
// pinned ESP32 LittleFS configuration. Linux otherwise permits 255-byte names.
#include "pro_journal.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_rename(const char*,const char*);
static bool tooLong(const char* path){const char* leaf=strrchr(path,'/');return strlen(leaf?leaf+1:path)>64;}
extern "C" FILE* __wrap_fopen(const char* p,const char* m){if(tooLong(p)){errno=ENAMETOOLONG;return nullptr;}return __real_fopen(p,m);}
extern "C" int __wrap_rename(const char* a,const char* b){if(tooLong(a)||tooLong(b)){errno=ENAMETOOLONG;return -1;}return __real_rename(a,b);}
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
  char temp[]="/tmp/csp-lfs-XXXXXX";CHECK(mkdtemp(temp));
  try{
    pro::FileJournal journal(temp);pro::Record r,loaded;
    r.path="sample.txt";r.digest=std::string(64,'a');r.destination=std::string(64,'b');r.size=9;
    std::string key(64,'c');CHECK(journal.load(key,loaded)==pro::Load::Missing);
    for(unsigned i=1;i<=10;++i){r.sequence=i;r.state=i<10?pro::State::Pending:pro::State::Verified;r.verifiedAt=i==10?2000000000:0;
      CHECK(journal.save(key,r));CHECK(journal.load(key,loaded)==pro::Load::Found);CHECK(loaded.sequence==i);CHECK(loaded.state==r.state);
    }
    for(const auto& e:std::filesystem::directory_iterator(temp))CHECK(e.path().filename().string().size()<=64);
    std::filesystem::remove_all(temp);puts("LittleFS 64-byte filename regression: save, replace, reload and verify passed");return 0;
  }catch(...){std::filesystem::remove_all(temp);throw;}
}
