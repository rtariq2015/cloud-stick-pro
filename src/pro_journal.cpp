#include "pro_journal.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>
#ifdef ARDUINO
#include <sdkconfig.h>
#endif

namespace pro {
std::string journalStem(const std::string& key) {
  if(!validHex(key.c_str()))return {};
  constexpr char alphabet[]="abcdefghijklmnopqrstuvwxyz234567";
  auto digit=[](char c){return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);};
  std::string out;out.reserve(52);uint32_t buffer=0;unsigned bits=0;
  for(size_t i=0;i<64;i+=2){
    buffer=(buffer<<8)|(digit(key[i])<<4)|digit(key[i+1]);bits+=8;
    while(bits>=5){bits-=5;out+=alphabet[(buffer>>bits)&31];}
  }
  if(bits)out+=alphabet[(buffer<<(5-bits))&31];return out;
}
struct CloseFile {void operator()(FILE* f)const{fclose(f);}};
struct CloseDirectory {void operator()(DIR* d)const{closedir(d);}};
using FileHandle=std::unique_ptr<FILE,CloseFile>;
using DirectoryHandle=std::unique_ptr<DIR,CloseDirectory>;
static Load read(const std::string& name,Record& r,bool legacy=false) {
  FileHandle file(fopen(name.c_str(),"rb"));FILE* f=file.get();
  if(!f)return errno==ENOENT||(legacy&&errno==ENAMETOOLONG)?Load::Missing:Load::IOError;
  uint8_t data[RecordSize];size_t n=fread(data,1,sizeof data,f);
  bool extra=fgetc(f)!=EOF;bool error=ferror(f);file.reset();
  if(error)return Load::IOError;
  return n==sizeof data && !extra && decodeRecord(data,r)?Load::Found:Load::Corrupt;
}
Load FileJournal::load(const std::string& key,Record& r) {
  if(!validHex(key.c_str()))return Load::IOError;
  std::string stem=journalStem(key);
  Record a,b;Load x=read(root_+"/"+stem+".0",a),y=read(root_+"/"+stem+".1",b);
  // Read legacy receipts on filesystems that supported their longer names.
  // New names take precedence; never fall back from a corrupt new generation.
#if !defined(ARDUINO) || CONFIG_LITTLEFS_OBJ_NAME_LEN >= 66
  if(x==Load::Missing&&y==Load::Missing){x=read(root_+"/"+key+".0",a,true);y=read(root_+"/"+key+".1",b,true);}
#endif
  if(x==Load::IOError||y==Load::IOError)return Load::IOError;
  if(x==Load::Found||y==Load::Found) {
    if(x==Load::Found && (y!=Load::Found || int32_t(a.sequence-b.sequence)>0))r=a;else r=b;
    return Load::Found;
  }
  return x==Load::Missing && y==Load::Missing?Load::Missing:Load::Corrupt;
}
Load FileJournal::next(const std::string& after,std::string& cursor,Record& r) {
  cursor.clear();
  if(!after.empty()&&(after.size()!=52||after.find_first_not_of("abcdefghijklmnopqrstuvwxyz234567")!=std::string::npos))return Load::IOError;
  DirectoryHandle directory(opendir(root_.c_str()));if(!directory)return Load::IOError;
  dirent* entry;
  while((entry=readdir(directory.get()))){
    std::string name=entry->d_name;
    if(name.size()!=54||name[52]!='.'||(name[53]!='0'&&name[53]!='1'))continue;
    std::string stem=name.substr(0,52);
    if(stem.find_first_not_of("abcdefghijklmnopqrstuvwxyz234567")!=std::string::npos)continue;
    if(stem>after&&(cursor.empty()||stem<cursor))cursor=stem;
  }
  directory.reset();if(cursor.empty())return Load::Missing;
  Record a,b;Load x=read(root_+"/"+cursor+".0",a),y=read(root_+"/"+cursor+".1",b);
  if(x==Load::IOError||y==Load::IOError)return Load::IOError;
  if(x==Load::Found||y==Load::Found){r=x==Load::Found&&(y!=Load::Found||int32_t(a.sequence-b.sequence)>0)?a:b;return Load::Found;}
  return Load::Corrupt;
}
bool FileJournal::room(const std::string& key) {
  Record existing;Load result=load(key,existing);
  if(result==Load::IOError)return false;
  if(result!=Load::Missing)return true;
  DirectoryHandle directory(opendir(root_.c_str()));DIR* dir=directory.get();if(!dir)return false;
  // Each identity has at most two generations. Count unique keys without RAM growth.
  size_t count=0;dirent* e;
  while((e=readdir(dir))) {
    std::string name=e->d_name;
    size_t stemSize=name.size()==54?52:name.size()==66?64:0;
    if(stemSize && name.substr(stemSize)==".0")++count;
    else if(stemSize && name.substr(stemSize)==".1") {
      FileHandle other(fopen((root_+"/"+name.substr(0,stemSize)+".0").c_str(),"rb"));
      if(!other)++count;
    }
  }
  return count<FileLimit;
}
bool FileJournal::save(const std::string& key,const Record& r) {
  if(!validHex(key.c_str())||!room(key))return false;
  uint8_t data[RecordSize];if(!encodeRecord(r,data))return false;
  std::string target=root_+"/"+journalStem(key)+"."+char('0'+(r.sequence&1));
  // A single fixed temporary file is safe: exactly one backup task owns this journal.
  std::string temp=root_+"/pending.tmp";
  FileHandle file(fopen(temp.c_str(),"wb"));FILE* f=file.get();if(!f)return false;
  bool ok=fwrite(data,1,sizeof data,f)==sizeof data;
  if(fflush(f)!=0)ok=false;
  if(fsync(fileno(f))!=0)ok=false;
  if(fclose(file.release())!=0)ok=false;
  if(!ok)return false;
  Record check;if(read(temp,check)!=Load::Found || check.sequence!=r.sequence)return false;
  if(rename(temp.c_str(),target.c_str())!=0)return false;
  // LittleFS commits rename atomically; POSIX test backend also syncs its directory.
#ifndef ARDUINO
  int fd=::open(root_.c_str(),O_RDONLY|O_DIRECTORY);if(fd<0)return false;
  ok=fsync(fd)==0;close(fd);if(!ok)return false;
#endif
  return read(target,check)==Load::Found && check.sequence==r.sequence;
}
}
