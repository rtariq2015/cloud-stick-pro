#pragma once
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
constexpr int WL_CONNECTED=3,WL_DISCONNECTED=6;

class IPAddress {
 public:
  IPAddress(uint32_t value=0):value_(value){}
  explicit operator uint32_t() const{return value_;}
  bool operator==(const IPAddress& other)const{return value_==other.value_;}
  bool operator!=(const IPAddress& other)const{return value_!=other.value_;}
  std::string toString()const{
    return std::to_string(value_&255)+"."+std::to_string((value_>>8)&255)+"."+
      std::to_string((value_>>16)&255)+"."+std::to_string(value_>>24);
  }
 private:uint32_t value_;
};
struct NativeSerial {
  std::string output;
  void println(const char* value){output+=value;output+='\n';}
  void printf(const char* format,...){
    char value[2048];va_list args;va_start(args,format);
    const int size=std::vsnprintf(value,sizeof value,format,args);va_end(args);
    if(size>0&&size<int(sizeof value))output.append(value,size);
  }
};
struct NativeWiFi {
  struct Station {bool present=true;void* netif(){return present?this:nullptr;}} STA;
  std::string name="Guest-Net";
  IPAddress address{0x0703020a},dns[2]{{0x0103020a},{0x08080808}};
  int signal=-62;
  int connection_status=WL_CONNECTED;
  IPAddress dnsIP(unsigned index)const{return dns[index];}
  IPAddress localIP()const{return address;}
  std::string SSID()const{return name;}
  int RSSI()const{return signal;}
  int status()const{return connection_status;}
};
extern NativeSerial USBSerial;
extern NativeWiFi WiFi;
