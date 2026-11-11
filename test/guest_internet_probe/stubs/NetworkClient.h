#pragma once
#include "Arduino.h"
#include "WiFi.h"
#include <algorithm>
#include <limits>
struct SocketPlan {
 bool connectOk=true;int tlsError=0;
 std::string response="HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nDO_NOT_READ_BODY";
 size_t writeLimit=std::numeric_limits<size_t>::max();
 uint32_t byteIntervalMs=0;
 bool holdOpen=false;
 int readErrorAt=-1;
};
struct SocketTrace {
 bool secure=false,bundle=false,insecure=false,stopped=false;
 unsigned handshakeTimeout=0;int connectTimeout=0;uint16_t port=0;
 std::string hostname,request;size_t readBytes=0;int stops=0;
};
inline SocketPlan plainPlan{true,0,"HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\nDO_NOT_READ_BODY"},securePlan;
inline std::vector<SocketTrace> requests;
inline std::function<void()> socketConnectHook,socketReadHook,socketWriteHook;
class NetworkClient {
protected:
 bool secure=false,bundle=false,insecure=false,open=false;
 unsigned handshakeTimeout=0;size_t traceIndex=0,cursor=0;uint32_t connectedAt=0;
 SocketPlan plan;
 int begin(const char* host,uint16_t port,int timeout){
  plan=secure?securePlan:plainPlan;cursor=0;connectedAt=testMillis;
  traceIndex=requests.size();requests.push_back({secure,bundle,insecure,false,handshakeTimeout,timeout,port,host,"",0,0});
  if(socketConnectHook)socketConnectHook();
  open=plan.connectOk;return open?1:0;
 }
public:
 virtual ~NetworkClient(){stop();}
 virtual int connect(IPAddress,uint16_t port,int32_t timeout){return begin("resolved IPv4",port,timeout);}
 virtual int connect(const char* host,uint16_t port,int32_t timeout){return begin(host,port,timeout);}
 virtual size_t write(const uint8_t* bytes,size_t length){
  if(socketWriteHook)socketWriteHook();
  if(!open)return 0;
  const size_t n=std::min(length,plan.writeLimit);requests[traceIndex].request.append(reinterpret_cast<const char*>(bytes),n);return n;
 }
 virtual size_t write(uint8_t byte){return write(&byte,1);}
 virtual int available(){
  if(!open||cursor>=plan.response.size())return 0;
  if(plan.byteIntervalMs&&uint32_t(testMillis-connectedAt)<cursor*plan.byteIntervalMs)return 0;
  return 1;
 }
 virtual int read(){
  if(socketReadHook)socketReadHook();
  if(plan.readErrorAt>=0&&cursor==size_t(plan.readErrorAt))return -1;
  if(!available())return -1;
  const auto value=static_cast<unsigned char>(plan.response[cursor++]);requests[traceIndex].readBytes=cursor;return value;
 }
 virtual uint8_t connected(){return open&&(cursor<plan.response.size()||plan.holdOpen);}
 virtual void stop(){if(open){open=false;requests[traceIndex].stopped=true;++requests[traceIndex].stops;}}
 void setTimeout(uint32_t){}
};
