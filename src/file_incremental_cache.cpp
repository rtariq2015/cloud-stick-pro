#include "file_incremental_cache.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace pro::incremental {
static void put64(uint8_t* p,uint64_t n){for(unsigned i=0;i<8;++i)p[i]=uint8_t(n>>(i*8));}
static uint64_t get64(const uint8_t* p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(i*8);return n;}
static void put32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
static uint32_t get32(const uint8_t* p){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(p[i])<<(i*8);return n;}
void Coverage::start(const std::array<uint8_t,16>& boot){boot_=boot;epoch_=1;last_=0;sealed_=0;used_=0;bool nonzero=false;for(auto b:boot_)nonzero|=b!=0;if(!nonzero)epoch_=0;}
void Coverage::invalidate(){used_=0;if(epoch_==UINT64_MAX)epoch_=0;else if(epoch_)++epoch_;}
void Coverage::wrote(uint64_t generation,uint32_t first,uint32_t last){
  if(!epoch_)return;
  if(generation<=last_||first>last){invalidate();last_=generation;return;}
  last_=generation;
  // Merge overlapping/adjacent writes conservatively using the newest stamp.
  for(size_t i=0;i<used_;){auto& r=ranges_[i];
    if(r.generation>sealed_&&uint64_t(first)<=uint64_t(r.last)+1&&uint64_t(r.first)<=uint64_t(last)+1){
      first=std::min(first,r.first);last=std::max(last,r.last);ranges_[i]=ranges_[--used_];i=0;
    }else ++i;
  }
  if(used_==RangeLimit)invalidate();
  if(epoch_)ranges_[used_++]={generation,first,last};
}
bool sameStamp(const Stamp& a,const Stamp& b){return a.epoch&&a.epoch==b.epoch&&a.boot==b.boot;}
bool Coverage::clean(const Stamp& old,uint64_t current,uint32_t first,uint32_t count)const{
  if(!sameStamp(old,stamp(current))||old.generation>current||last_>current||!count||uint64_t(first)+count>uint64_t(UINT32_MAX)+1)return false;
  const uint64_t end=uint64_t(first)+count;
  for(size_t i=0;i<used_;++i){const auto& r=ranges_[i];if(r.generation>old.generation&&r.first<end&&r.last>=first)return false;}
  return true;
}
bool digest(const std::string& text,Digest& out){if(text.size()!=64||!validHex(text.c_str()))return false;for(size_t i=0;i<32;++i){auto v=[](char c)->unsigned{return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);};out[i]=uint8_t(v(text[2*i])*16+v(text[2*i+1]));}return true;}
bool encode(const Entry& e,uint8_t out[EntryBytes]){
  if(!e.stamp.epoch)return false;memset(out,0,EntryBytes);memcpy(out,"CIC1",4);memcpy(out+4,e.stamp.boot.data(),16);
  put64(out+20,e.stamp.epoch);put64(out+28,e.stamp.generation);put64(out+36,e.size);put32(out+44,e.auditedAt);
  memcpy(out+48,e.scope.data(),32);memcpy(out+80,e.key.data(),32);memcpy(out+112,e.receipt.data(),32);memcpy(out+144,e.layout.data(),32);
  put32(out+188,crc32(out,188));return true;
}
bool decode(const uint8_t in[EntryBytes],Entry& e){
  if(memcmp(in,"CIC1",4)||get32(in+188)!=crc32(in,188))return false;
  for(unsigned i=176;i<188;++i)if(in[i])return false;
  Entry next;memcpy(next.stamp.boot.data(),in+4,16);next.stamp.epoch=get64(in+20);next.stamp.generation=get64(in+28);next.size=get64(in+36);next.auditedAt=get32(in+44);
  memcpy(next.scope.data(),in+48,32);memcpy(next.key.data(),in+80,32);memcpy(next.receipt.data(),in+112,32);memcpy(next.layout.data(),in+144,32);
  if(!next.stamp.epoch)return false;e=next;return true;
}
bool eligible(const Entry& e,const Entry& expected,uint32_t now){return sameStamp(e.stamp,expected.stamp)&&e.stamp.generation<=expected.stamp.generation&&e.size==expected.size&&e.scope==expected.scope&&e.key==expected.key&&e.receipt==expected.receipt&&uint32_t(now-e.auditedAt)<AuditMs;}
bool Store::load(const Digest& key,Entry& entry)const{
  FILE* f=fopen(path_.c_str(),"rb");if(!f)return false;uint8_t bytes[EntryBytes];
  bool ok=fseek(f,0,SEEK_END)==0;long size=ok?ftell(f):-1;
  ok=ok&&size>=0&&size<=long(SlotCount*EntryBytes)&&fseek(f,0,SEEK_SET)==0;
  bool found=false;
  for(long slot=0;ok&&slot<size/long(EntryBytes);++slot){
    ok=fread(bytes,1,sizeof bytes,f)==sizeof bytes&&!ferror(f);Entry next;
    if(ok&&decode(bytes,next)&&next.key==key){entry=next;found=true;break;}
  }
  if(fclose(f))ok=false;return ok&&found;
}
bool Store::save(const Entry& entry)const{
  uint8_t bytes[EntryBytes];if(!encode(entry,bytes))return false;
  FILE* f=fopen(path_.c_str(),"r+b");if(!f)f=fopen(path_.c_str(),"w+b");if(!f)return false;
  // This file has no authority over receipts. Interrupted writes fail its CRC;
  // failure never changes upload/verification results or triggers formatting.
  bool ok=fseek(f,0,SEEK_END)==0;long size=ok?ftell(f):-1;
  ok=ok&&size>=0&&size<=long(SlotCount*EntryBytes)&&fseek(f,0,SEEK_SET)==0;
  size_t target=SlotCount,free=SlotCount;uint8_t prior[EntryBytes];
  for(size_t slot=0;ok&&slot<size_t(size)/EntryBytes;++slot){
    ok=fread(prior,1,sizeof prior,f)==sizeof prior&&!ferror(f);Entry decoded;
    if(!ok)break;
    if(!decode(prior,decoded)){if(free==SlotCount)free=slot;}
    else if(decoded.key==entry.key){target=slot;break;}
  }
  if(target==SlotCount)target=free!=SlotCount?free:std::min(size_t(size<0?0:size)/EntryBytes,SlotCount-1);
  // At capacity a new identity conservatively replaces one hint, never a receipt.
  ok=ok&&fseek(f,long(target*EntryBytes),SEEK_SET)==0&&fwrite(bytes,1,sizeof bytes,f)==sizeof bytes&&fflush(f)==0;
  if(fclose(f))ok=false;return ok;
}
}
