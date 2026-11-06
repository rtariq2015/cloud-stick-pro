#pragma once
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <map>
#include <functional>
#include <unistd.h>
namespace event_io {
inline std::string failure;
inline std::map<FILE*,std::string> paths;
inline unsigned writes=0,renames=0,opens=0;
inline bool readError=false;
inline std::function<void(const char*)> before;
inline void touch(const char* operation){if(before)before(operation);}
inline FILE* open(const char* path,const char* mode){
 touch("open");++opens;
 if((failure=="open-write"&&mode[0]=='w')||(failure=="open-read"&&mode[0]=='r')){errno=EACCES;return nullptr;}
 FILE* file=std::fopen(path,mode);if(file)paths[file]=path;return file;
}
inline size_t read(void* data,size_t size,size_t count,FILE* file){
 touch("read");if(failure=="read"){readError=true;return 0;}
 const auto result=std::fread(data,size,count,file);
 if(failure=="readback"&&paths[file].find("events.tmp")!=std::string::npos&&result)static_cast<unsigned char*>(data)[0]^=1;
 return result;
}
inline int error(FILE* file){return readError?1:std::ferror(file);}
inline size_t write(const void* data,size_t size,size_t count,FILE* file){
 touch("write");++writes;
 if(failure=="write")return std::fwrite(data,size,count/2,file);
 return std::fwrite(data,size,count,file);
}
inline int flush(FILE* file){touch("flush");const int result=std::fflush(file);return failure=="flush"?EOF:result;}
inline int sync(int fd){touch("sync");return failure=="sync"?-1: ::fsync(fd);}
inline int close(FILE* file){touch("close");paths.erase(file);const int result=std::fclose(file);return failure=="close"?EOF:result;}
inline int rename(const char* old,const char* next){touch("rename");++renames;if(failure=="rename"){errno=EIO;return -1;}return std::rename(old,next);}
inline void clear(){failure.clear();readError=false;before={};writes=renames=opens=0;}
}
#define CSP_EVENT_OPEN ::event_io::open
#define CSP_EVENT_READ ::event_io::read
#define CSP_EVENT_ERROR ::event_io::error
#define CSP_EVENT_WRITE ::event_io::write
#define CSP_EVENT_FLUSH ::event_io::flush
#define CSP_EVENT_SYNC ::event_io::sync
#define CSP_EVENT_CLOSE ::event_io::close
#define CSP_EVENT_RENAME ::event_io::rename
