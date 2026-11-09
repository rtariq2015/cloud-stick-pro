// SPDX-License-Identifier: MIT
#include "ble_wake_runtime.h"
#include "wifi_setup_runtime.h"
#include "backup_worker.h"
#include "lan_wake.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <nvs.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using namespace pro::ble_wake;
namespace {
Record stored{};bool exists=false,authorized=true,barrier=false,paused=true,worker=false,bad_commit=false;
unsigned writes=0,random_calls=0;
template<size_t N>std::array<uint8_t,N> parse(const std::string& text){
  assert(text.size()==N*2);std::array<uint8_t,N> out{};for(size_t i=0;i<N;++i)out[i]=uint8_t(std::stoul(text.substr(2*i,2),nullptr,16));return out;
}
}
int nvs_open(const char* name,int mode,nvs_handle_t* handle){assert(!std::strcmp(name,"csp-wake"));*handle=7;return !exists&&mode==NVS_READONLY?-1:ESP_OK;}
int nvs_get_blob(nvs_handle_t handle,const char* name,void* bytes,size_t* size){assert(handle==7&&!std::strcmp(name,"phone")&&*size==RecordBytes);if(!exists)return -1;std::memcpy(bytes,stored.data(),stored.size());return ESP_OK;}
int nvs_set_blob(nvs_handle_t handle,const char* name,const void* bytes,size_t size){assert(handle==7&&!std::strcmp(name,"phone")&&size==RecordBytes);++writes;std::memcpy(stored.data(),bytes,size);exists=true;return ESP_OK;}
int nvs_commit(nvs_handle_t handle){assert(handle==7);return bad_commit?-1:ESP_OK;}
void nvs_close(nvs_handle_t handle){assert(handle==7);}
void esp_fill_random(void* out,size_t size){assert(size==NonceBytes);std::memset(out,0x42+random_calls++,size);}
bool ble_management_authorizes(const WifiSetupSession& session){return authorized&&session.id==1&&session.generation==2;}
bool wifi_setup_owner_login_info_allowed(){return !barrier;}
bool maintenance_active(){return paused;}
BackupWorkerSnapshot backup_worker_snapshot(){return {worker?BackupWorkerPhase::Running:BackupWorkerPhase::Stopped};}
namespace {
constexpr uint16_t Port=18081;
constexpr uint32_t Address=0x0100007f; // loopback, never a physical device
uint32_t clock_tick=100000;
int exchange(Server& server,const std::string& request,std::string& response){
  server.poll(++clock_tick,true,Address,true);
  const int fd=::socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
  sockaddr_in peer{};peer.sin_family=AF_INET;peer.sin_port=htons(Port);peer.sin_addr.s_addr=Address;
  assert(::connect(fd,reinterpret_cast<sockaddr*>(&peer),sizeof peer)==0);
  assert(::send(fd,request.data(),request.size(),MSG_NOSIGNAL)==int(request.size()));
  assert(::fcntl(fd,F_SETFL,O_NONBLOCK)==0);
  response.clear();
  for(unsigned i=0;i<150;++i){
    server.poll(++clock_tick,true,Address,true);char bytes[1024];const auto n=::recv(fd,bytes,sizeof bytes,0);
    if(n>0)response.append(bytes,size_t(n));
    const auto header=response.find("\r\n\r\n"),length=response.find("Content-Length: ");
    if(header!=std::string::npos&&length!=std::string::npos&&response.size()>=header+4+std::stoul(response.substr(length+16)))return fd;
    ::usleep(1000);
  }
  assert(false&&"bounded loopback reply missing");return -1;
}
void finish(Server& server,int fd){assert(::close(fd)==0);for(unsigned i=0;i<5;++i){server.poll(++clock_tick,true,Address,true);::usleep(1000);}}
}
int main(){
  std::signal(SIGPIPE,SIG_IGN);
  std::string pub,id_text,nonce_text,signature_text,replacement_text;std::getline(std::cin,pub);std::getline(std::cin,id_text);std::getline(std::cin,nonce_text);std::getline(std::cin,signature_text);std::getline(std::cin,replacement_text);
  const auto key=parse<65>(pub);const auto expected_id=parse<16>(id_text);const auto expected_nonce=parse<32>(nonce_text);
  std::array<uint8_t,72> signature{};const size_t signature_size=signature_text.size()/2;assert(signature_size>=8&&signature_size<=72);
  for(size_t i=0;i<signature_size;++i)signature[i]=uint8_t(std::stoul(signature_text.substr(2*i,2),nullptr,16));
  const std::array<uint8_t,6> device{1,2,3,4,5,6};initialize(device,123);assert(!paired());
  WifiSetupSession session{1,2};Id id{};
  authorized=false;assert(!pairPhone(session,key,id)&&writes==0);authorized=true;
  barrier=true;assert(!pairPhone(session,key,id)&&writes==0);barrier=false;
  worker=true;assert(!pairPhone(session,key,id)&&writes==0);worker=false;
  paused=false;assert(!pairPhone(session,key,id)&&writes==0);paused=true;
  PublicKey invalid{};invalid[0]=4;assert(!pairPhone(session,invalid,id)&&writes==0);
  assert(pairPhone(session,key,id)&&id==expected_id&&paired()&&writes==1);
  assert(pairPhone(session,key,id)&&writes==1); // exact retries do not wear flash
  Nonce nonce;assert(challenge(1000,true,nonce)==Result::Ready&&nonce==expected_nonce);
  assert(authorize(1100,true,nonce,signature.data(),signature_size)==Result::Ready);
  assert(authorize(40000,true,nonce,signature.data(),signature_size)==Result::Expired);
  assert(challenge(40000,true,nonce)==Result::Ready&&nonce!=expected_nonce);
  assert(authorize(40100,true,nonce,signature.data(),signature_size)==Result::Denied);
  const auto saved=stored;stored.back()^=1;initialize(device,123);assert(!paired());stored=saved;
  initialize(device,124);assert(!paired());initialize({6,5,4,3,2,1},123);assert(!paired());
  initialize(device,123);assert(paired());revoke(124);assert(!paired()&&challenge(90000,true,nonce)==Result::Unavailable);initialize(device,123);assert(paired());bad_commit=true;const auto replacement=parse<65>(replacement_text);
  assert(!pairPhone(session,replacement,id)&&!paired()&&writes==2);
  // A failed commit may leave a matching cached value. Explicit retry must
  // still confirm a successful commit before granting wake authority.
  bad_commit=false;assert(pairPhone(session,replacement,id)&&paired()&&writes==3);
  stored=saved;random_calls=0;initialize(device,123);assert(paired());Server server(Port);std::string response;
  const std::string get="GET /challenge HTTP/1.1\r\nHost: 127.0.0.1:18081\r\nConnection: close\r\n\r\n";
  int fd=exchange(server,get,response);assert(response.find("200 OK")!=std::string::npos&&response.find(nonce_text)!=std::string::npos);assert(!server.takeRequest());finish(server,fd);assert(!server.takeRequest());
  const std::string body="{\"v\":1,\"id\":\""+id_text+"\",\"nonce\":\""+nonce_text+"\",\"signature\":\""+signature_text+"\"}";
  const std::string post="POST /open HTTP/1.1\r\nHost: 127.0.0.1:18081\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
  fd=exchange(server,post,response);assert(response.find("200 OK")!=std::string::npos&&response.find("opening")!=std::string::npos);
  assert(!server.takeRequest());finish(server,fd);assert(server.takeRequest());assert(!server.takeRequest()); // one grant, only after passive close
  fd=exchange(server,post,response);assert(response.find("200 OK")==std::string::npos);finish(server,fd);assert(!server.takeRequest());
  const auto origin=get.substr(0,get.size()-2)+"Origin: http://evil.example\r\n\r\n";
  fd=exchange(server,origin,response);assert(response.find("400 Bad Request")!=std::string::npos);finish(server,fd);assert(!server.takeRequest());
  // Even a valid proof is discarded when the peer never completes its close.
  random_calls=0;initialize(device,123);fd=exchange(server,get,response);finish(server,fd);
  fd=exchange(server,post,response);assert(response.find("opening")!=std::string::npos);server.poll(clock_tick+501,true,Address,true);clock_tick+=501;assert(!server.takeRequest());finish(server,fd);assert(server.stop());
  std::cout<<"PASS real loopback HTTP/P-256 proof, passive close, replay and timeout; hardware/NVS/auth/RNG simulated\n";
  std::cout<<"PASS actual mbedTLS verifies Java P-256 proof; NVS/radio/auth/RNG simulated, owner binding and write gates retained\n";
}
