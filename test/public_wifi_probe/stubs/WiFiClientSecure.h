#pragma once
#include "probe_state.h"
#include <cassert>
class WiFiClientSecure{
 public:
  WiFiClientSecure(){++probe_test::state.liveTLS;}
  virtual ~WiFiClientSecure(){--probe_test::state.liveTLS;}
  bool trusted=false;
  void setCACert(const char* value){trusted=value&&std::string(value)=="synthetic-trusted-ca";}
  void setHandshakeTimeout(unsigned seconds){assert(seconds==10);}
  void setTimeout(unsigned ms){assert(ms==15000);}
  virtual uint8_t connected(){return 1;}
  virtual int available(){return 0;}
  virtual int read(uint8_t*,size_t){return -1;}
  virtual int read(){return -1;}
  virtual void stop(){}
};
