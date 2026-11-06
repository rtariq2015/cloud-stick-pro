#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define PROGMEM

class String:public std::string {
 public:
  using std::string::string;
  String()=default;
  String(size_t n):std::string(std::to_string(n)){}
  String(const std::string& value):std::string(value){}
  bool endsWith(const char* suffix)const{auto n=std::strlen(suffix);return size()>=n&&compare(size()-n,n,suffix)==0;}
  bool startsWith(const char* prefix)const{return rfind(prefix,0)==0;}
  void trim(){const auto first=find_first_not_of(" \t\r\n");if(first==npos)clear();else *this=substr(first,find_last_not_of(" \t\r\n")-first+1);}
};
class Stream {
  int writeError_=0;
 protected:void setWriteError(int error=1){writeError_=error;}
 public:
  int getWriteError(){return writeError_;}void clearWriteError(){writeError_=0;}
  virtual ~Stream()=default;
  virtual int available()=0;
  virtual int read()=0;
  virtual int peek()=0;
  virtual void flush()=0;
  virtual size_t write(uint8_t)=0;
  virtual size_t write(const uint8_t* data,size_t size){size_t done=0;while(done<size&&write(data[done]))++done;return done;}
  virtual size_t readBytes(char* data,size_t size){size_t done=0;int c;while(done<size&&(c=read())>=0)data[done++]=char(c);return done;}
  virtual size_t readBytes(uint8_t* data,size_t size){return readBytes(reinterpret_cast<char*>(data),size);}
};
extern uint32_t fakeNow;
inline uint32_t millis(){return fakeNow;}
inline void vTaskDelay(unsigned delay){fakeNow+=delay;}
struct FakeESP {uint64_t getEfuseMac()const{return 0x001122334455ULL;}};
inline FakeESP ESP;

inline void delay(unsigned n){fakeNow+=n;}
