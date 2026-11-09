// SPDX-License-Identifier: MIT
#pragma once
#include "ble_wake.h"
namespace pro::ble_wake {
enum class Route:uint8_t {Invalid,Challenge,Open};
struct Frame {Route route=Route::Invalid;size_t body=0,header=0;};
// Complete strict request header, exact STA Host including the fixed port.
// Canonical proof JSON has no escaping, duplicate fields or ambiguous framing.
bool request(const char*,size_t,const char*,Frame&);
bool proof(const char*,size_t,Id&,Nonce&,std::array<uint8_t,72>&,size_t&);
#if CSP_ENABLE_BLE_WIFI_WAKE
class Server {
 public:
  explicit Server(uint16_t port=81):port_(port){}
  ~Server();
  Server(const Server&)=delete;
  Server& operator=(const Server&)=delete;
  void poll(uint32_t,bool,uint32_t,bool);
  bool takeRequest();
  bool stop();
  bool faulted()const{return fault_;}
 private:
  struct Buffers;
  Buffers* buffers_=nullptr;
  uint16_t port_;
  int listener_=-1,client_=-1;
  uint32_t address_=0,accepted_=0,retry_=0,finished_=0;
  size_t received_=0,header_=0,total_=0,sent_=0;
  bool parsed_=false,sending_=false,waiting_=false,grant_=false,ready_=false,fault_=false;
  char host_[24]{};
  bool start(uint32_t);
  bool closeClient();
  void respond(int,const char*);
  void dispatch(uint32_t,bool);
};
void poll(uint32_t,bool,uint32_t,bool);
bool takeRequest();
bool stop();
#endif
}
