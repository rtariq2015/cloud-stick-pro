#pragma once
#include <cassert>
extern unsigned liveTLS;
class WiFiClientSecure {
 public:
  WiFiClientSecure(){++liveTLS;}
  virtual ~WiFiClientSecure(){--liveTLS;}
  bool trusted=false;
  void setCACert(const char* value){trusted=value&&*value;}
  void setHandshakeTimeout(unsigned timeout){assert(timeout==10);}
  void setTimeout(unsigned timeout){assert(timeout==15000);}
  virtual uint8_t connected(){return 1;}
  virtual int available(){return 0;}
  virtual int read(uint8_t*,size_t){return -1;}
  virtual int read(){return -1;}
  virtual void stop(){}
};
