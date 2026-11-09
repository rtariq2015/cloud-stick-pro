// SPDX-License-Identifier: MIT
#include "lan_wake.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace pro::ble_wake;
int main(){
  constexpr const char* host="192.168.1.42:81";
  const std::string get="GET /challenge HTTP/1.1\r\nHost: 192.168.1.42:81\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
  Frame frame;assert(request(get.data(),get.size(),host,frame)&&frame.route==Route::Challenge&&frame.body==0);
  const std::string prefix="{\"v\":1,\"id\":\"",middle="\",\"nonce\":\"",last="\",\"signature\":\"";
  const std::string valid=prefix+std::string(32,'1')+middle+std::string(64,'2')+last+std::string(16,'3')+"\"}";
  Id id;Nonce nonce;std::array<uint8_t,72> signature;size_t length=0;
  assert(proof(valid.data(),valid.size(),id,nonce,signature,length)&&length==8&&id[0]==0x11&&nonce[0]==0x22&&signature[0]==0x33);
  const std::string post="POST /open HTTP/1.1\r\nHost: 192.168.1.42:81\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(valid.size())+"\r\n\r\n";
  assert(request(post.data(),post.size(),host,frame)&&frame.route==Route::Open&&frame.body==valid.size());
  for(const std::string& body:std::initializer_list<std::string>{"",valid+"{}",std::string(" ")+valid,valid.substr(0,valid.size()-1),prefix+std::string(32,'A')+middle+std::string(64,'2')+last+std::string(16,'3')+"\"}",prefix+std::string(32,'1')+middle+std::string(64,'2')+last+std::string(146,'3')+"\"}"})assert(!proof(body.data(),body.size(),id,nonce,signature,length));
  for(const std::string header:{
      "GET /challenge HTTP/1.1\r\nHost: other:81\r\n\r\n",
      "GET /challenge HTTP/1.1\r\nHost: 192.168.1.42:81\r\nHost: 192.168.1.42:81\r\n\r\n",
      "GET /challenge HTTP/1.1\r\nHost: 192.168.1.42:81\r\nContent-Length: 0\r\n\r\n",
      "GET /challenge?x HTTP/1.1\r\nHost: 192.168.1.42:81\r\n\r\n",
      "GET http://192.168.1.42:81/challenge HTTP/1.1\r\nHost: 192.168.1.42:81\r\n\r\n",
      "GET /challenge HTTP/1.1\r\nHost: 192.168.1.42:81\r\nOrigin: http://evil.example\r\n\r\n",
      "GET /challenge HTTP/1.1\r\nHost: 192.168.1.42:81\r\nUser-Agent: value\nCookie: ignored\r\n\r\n",
      "GET /challenge HTTP/1.1\r\nHost : 192.168.1.42:81\r\n\r\n",
      "POST /open HTTP/1.1\r\nHost: 192.168.1.42:81\r\nContent-Type: application/json\r\nContent-Length: 01\r\n\r\n",
      "POST /open HTTP/1.1\r\nHost: 192.168.1.42:81\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"})assert(!request(header.data(),header.size(),host,frame)&&frame.route==Route::Invalid);
  assert(!request((post+valid).data(),post.size()+valid.size(),host,frame));
  std::cout<<"PASS wake HTTP dialect: strict host/method/framing, cross-origin and canonical proof boundaries\n";
}
