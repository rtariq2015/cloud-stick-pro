#pragma once
#include <Arduino.h>
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
enum class ReadMode {Normal,NegativeIdle,ZeroIdle,AvailableZero};
struct BodyNetwork {
 std::string bytes;size_t offset=0;ReadMode mode=ReadMode::Normal;bool stayOpen=false,closed=false;
 unsigned reads=0,stops=0;std::function<void()> poll;
};
extern BodyNetwork bodyNetwork;
extern unsigned liveTLS;
class NetworkClient:public Stream {
 protected:uint32_t _timeout=15000,_startMillis=0;
 int timedRead();
 public:
 virtual uint8_t connected()=0;
 int read()override=0;
 virtual int read(uint8_t*,size_t)=0;
 size_t readBytes(char*,size_t)override;
 size_t readBytes(uint8_t* b,size_t n)override{return readBytes(reinterpret_cast<char*>(b),n);}
 String readStringUntil(char);
 unsigned long getTimeout(){return _timeout;}
 int peek()override{return -1;}void flush()override{}
 size_t write(uint8_t)override{return 1;}
};
class WiFiClientSecure:public NetworkClient {
 public:
 WiFiClientSecure(){++liveTLS;}~WiFiClientSecure()override{--liveTLS;}
 bool trusted=false;
 void setCACert(const char* p){trusted=p&&std::string(p)=="synthetic-ca";}
 void setHandshakeTimeout(unsigned t){assert(t==10);}
 void setTimeout(unsigned t){assert(t==15000);_timeout=t;}
 uint8_t connected()override{return !bodyNetwork.closed&&(bodyNetwork.stayOpen||bodyNetwork.offset<bodyNetwork.bytes.size());}
 int available()override{
  if(bodyNetwork.poll)bodyNetwork.poll();
  if(bodyNetwork.closed)return 0;
  return int(bodyNetwork.bytes.size()-bodyNetwork.offset)+(bodyNetwork.mode==ReadMode::AvailableZero?1:0);
 }
 int read(uint8_t* bytes,size_t n)override{
  ++bodyNetwork.reads;if(bodyNetwork.poll)bodyNetwork.poll();
  if(bodyNetwork.closed)return -1;
  if(bodyNetwork.mode==ReadMode::AvailableZero)return 0;
  const auto count=std::min(n,bodyNetwork.bytes.size()-bodyNetwork.offset);
  if(count){std::memcpy(bytes,bodyNetwork.bytes.data()+bodyNetwork.offset,count);bodyNetwork.offset+=count;return int(count);}
  return bodyNetwork.mode==ReadMode::ZeroIdle?0:-1;
 }
 int read()override{uint8_t b;return read(&b,1)==1?b:-1;}
 void stop(){bodyNetwork.closed=true;++bodyNetwork.stops;}
};
