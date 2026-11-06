#pragma once
#include <Arduino.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <cassert>
#include <memory>
struct sslclient_context {mbedtls_ssl_context ssl_ctx;unsigned long socket_timeout=10000;};
int send_ssl_data(sslclient_context*,const uint8_t*,size_t);
inline int send_net_data(sslclient_context*,const uint8_t*,size_t){assert(false);return -1;}
constexpr int SO_SNDTIMEO=1;
extern unsigned liveTLS;
class NetworkClientSecure {
 protected:
  std::shared_ptr<sslclient_context> sslclient=std::make_shared<sslclient_context>();
  bool _connected=true,_stillinPlainStart=false;
  int _timeout=10000,_lastWriteTimeout=0;
 public:
  NetworkClientSecure(){++liveTLS;}virtual ~NetworkClientSecure(){--liveTLS;}
  bool trusted=false;
  void setCACert(const char* value){trusted=value&&*value;}
  void setHandshakeTimeout(unsigned n){assert(n==10);}
  void setTimeout(unsigned n){assert(n==15000);}
  virtual uint8_t connected(){return _connected;}
  virtual int available(){return 0;}
  virtual int read(uint8_t*,size_t){return -1;}
  virtual int read(){return -1;}
  virtual void stop(){_connected=false;}
  int getWriteError(){return 0;}
  void clearWriteError(){}
  int setSocketOption(int,char*,size_t){return 0;}
  size_t write(const uint8_t*,size_t);
};
using WiFiClientSecure=NetworkClientSecure;
