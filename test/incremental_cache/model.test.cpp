#include "file_incremental_cache.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
using namespace pro::incremental;
int main(int argc,char** argv){
 assert(argc==2);Coverage c;std::array<uint8_t,16> boot{};boot[0]=1;c.start(boot);
 const auto original=c.stamp(0);assert(c.clean(original,0,100,10));
 c.wrote(1,200,201);assert(c.clean(original,1,100,10));assert(!c.clean(original,1,200,1));
 c.wrote(2,99,100);assert(!c.clean(original,2,100,10));c.seal(2);
 auto old=c.stamp(2);
 c.wrote(3,101,101);assert(c.clean(old,3,100,1));c.seal(3);old=c.stamp(3);
 // Repeated adjacent new-file writes never grow old cohorts across a scan.
 for(uint64_t i=4;i<60;++i){c.wrote(i,uint32_t(98+i),uint32_t(98+i));assert(c.clean(old,i,uint32_t(97+i),1));c.seal(i);old=c.stamp(i);}
 c.invalidate();assert(!c.clean(old,60,100,1));
 auto now=c.stamp(60);for(uint64_t i=61;i<130;++i){c.wrote(i,uint32_t(i*4),uint32_t(i*4));c.seal(i);}assert(!c.clean(now,129,10,1));
 now=c.stamp(129);c.wrote(129,1,1);assert(!c.clean(now,129,10,1));
 c.start(boot);now=c.stamp(0);boot[0]=2;c.start(boot);assert(!c.clean(now,0,1,1));
 c.start({});assert(!c.clean(c.stamp(0),0,1,1));
 Entry e;e.stamp={boot,1,5};e.size=500;e.auditedAt=UINT32_MAX-100;e.key[0]=3;e.scope[0]=8;e.receipt[0]=9;e.layout[0]=4;
 uint8_t bytes[EntryBytes];assert(encode(e,bytes));Entry decoded;assert(decode(bytes,decoded));
 assert(eligible(decoded,e,50));assert(!eligible(decoded,e,e.auditedAt+AuditMs));
 auto changed=e;++changed.receipt[0];assert(!eligible(decoded,changed,50));
 changed=e;++changed.scope[0];assert(!eligible(decoded,changed,50));changed=e;++changed.size;assert(!eligible(decoded,changed,50));
 changed=e;++changed.stamp.epoch;assert(!eligible(decoded,changed,50));changed=e;changed.stamp.generation=4;assert(!eligible(decoded,changed,50));
 bytes[150]^=1;assert(!decode(bytes,decoded));assert(encode(e,bytes));bytes[180]=1;assert(!decode(bytes,decoded));
 Store store(argv[1]);assert(!store.load(e.key,decoded));assert(store.save(e));assert(store.load(e.key,decoded));
 auto collision=e;collision.key[1]=2;assert(store.save(collision));assert(store.load(e.key,decoded));assert(store.load(collision.key,decoded));
 for(unsigned i=0;i<100;++i){auto next=e;next.key[1]=uint8_t(i+3);assert(store.save(next));}
 for(unsigned i=0;i<100;++i){auto next=e;next.key[1]=uint8_t(i+3);assert(store.load(next.key,decoded)&&decoded.key==next.key);}
 FILE* f=fopen(argv[1],"wb");assert(f);assert(fwrite(bytes,1,17,f)==17);assert(!fclose(f));assert(!store.load(e.key,decoded));
 std::cout<<"PASS coverage/cohort/overflow/reboot/codec/collision/corruption/audit/wrap; Coverage="<<sizeof(Coverage)<<" Entry="<<sizeof(Entry)<<" bytes\n";
}
