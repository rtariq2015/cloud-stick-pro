#include "pro_core.h"
#include <cassert>
#include <iostream>
struct Local:pro::Local {
 unsigned hashes=0,reuses=0;
 bool fingerprint(const std::string&,std::string& hash,uint64_t& size)override{++hashes;hash=std::string(64,'a');size=100;return true;}
 std::string key(const std::string&)override{return std::string(64,'b');}
 bool reuseVerified(const pro::Record&)override{++reuses;return true;}
};
struct Journal:pro::Journal {
 pro::Record value;bool present=false;
 pro::Load load(const std::string&,pro::Record& out)override{out=value;return present?pro::Load::Found:pro::Load::Missing;}
 bool save(const std::string&,const pro::Record& next)override{value=next;present=true;return true;}
};
struct Remote:pro::Remote {unsigned puts=0,gets=0;pro::Result put(const std::string&,uint64_t)override{++puts;return {true,0};}pro::Result verify(const std::string&,const std::string&,uint64_t)override{++gets;return {true,0};}};
int main(){Local local;Journal journal;Remote remote;pro::Backup backup(local,journal,remote,std::string(64,'c'));
 assert(backup.process("QA/old.pdf",100).outcome==pro::Outcome::Verified);
 assert(backup.process("QA/old.pdf",101).outcome==pro::Outcome::Verified);
 assert(local.hashes==2&&local.reuses==0&&remote.puts==1&&remote.gets==1);
 std::cout<<"PASS default-disabled Backup fingerprints every candidate and ignores optional reuse hook\n";
}
