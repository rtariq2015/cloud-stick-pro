#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <functional>
#include <atomic>
#define PROGMEM
#define log_v(...) ((void)0)
#define log_d(...) ((void)0)
#define log_e(...) ((void)0)
class String:public std::string {
 public:
  using std::string::string;
  String()=default;String(const std::string& value):std::string(value){}
  explicit String(size_t value):std::string(std::to_string(value)){}
  bool endsWith(const char* suffix)const{auto n=std::strlen(suffix);return size()>=n&&compare(size()-n,n,suffix)==0;}
  bool startsWith(const char* prefix)const{return rfind(prefix,0)==0;}
};
class Stream {
  int writeError_=0;
 protected:void setWriteError(int error=1){writeError_=error;}
 public:
  int getWriteError(){return writeError_;}void clearWriteError(){writeError_=0;}
  virtual ~Stream()=default;
  virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;
  virtual size_t write(uint8_t)=0;
  virtual size_t write(const uint8_t* bytes,size_t n){size_t done=0;while(done<n&&write(bytes[done]))++done;return done;}
  virtual size_t readBytes(char* bytes,size_t n){size_t done=0;int value;while(done<n&&(value=read())>=0)bytes[done++]=char(value);return done;}
  virtual size_t readBytes(uint8_t* bytes,size_t n){return readBytes(reinterpret_cast<char*>(bytes),n);}
};
extern std::atomic<uint32_t> fakeNow;
inline uint32_t millis(){return fakeNow.load(std::memory_order_relaxed);}
inline void vTaskDelay(unsigned n){fakeNow+=n;}
inline void delay(unsigned n){fakeNow+=n;}
struct FakeESP {uint64_t getEfuseMac()const{return 0x001122334455ULL;}};
inline FakeESP ESP;
