#pragma once
#include <cassert>
#include <algorithm>
namespace dav_fixture {inline unsigned liveTLS=0,maxTLS=0;}
class WiFiClientSecure {
 public:
  WiFiClientSecure(){using namespace dav_fixture;++liveTLS;maxTLS=std::max(maxTLS,liveTLS);assert(liveTLS<=1);}
  virtual ~WiFiClientSecure(){assert(dav_fixture::liveTLS);--dav_fixture::liveTLS;}
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
