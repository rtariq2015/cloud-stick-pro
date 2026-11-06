#pragma once
#define CloudConfig WorkerFixtureConfig
#define WebDAVClient WorkerFixtureDAV
#define FakeWiFi WorkerFixtureWiFi
#define WiFi workerFixtureWiFi
#include "../../backup_integration/stubs/fakes.h"
#undef WiFi
#undef FakeWiFi
#undef WebDAVClient
#undef CloudConfig
#include <arpa/inet.h>
constexpr int ESP_OK=0;
namespace probe_fake {
inline unsigned loads=0,calls=0,clients=0;
inline bool valid=true,ok=true,connected=true,have_netif=true;
inline int code=207;
inline std::atomic<bool> entered{false},hold{false};
}
struct CloudConfig:WorkerFixtureConfig {~CloudConfig(){fake::event("probe config destroyed");}};
inline CloudConfig load_backup_config(){
  ++probe_fake::loads;CloudConfig c;c.is_valid=probe_fake::valid;c.server_url="https://synthetic.invalid/readonly/";
  c.username="synthetic-user";c.password="synthetic-secret";c.ca_pem="synthetic-ca";return c;
}
struct WebDAVClient {
  CloudConfig held;
  explicit WebDAVClient(const CloudConfig& c):held(c){++probe_fake::clients;}
  ~WebDAVClient(){fake::event("probe client destroyed");}
  struct Response{bool ok;int code;};
  Response probeConnectionReadOnly(bool(*cancelled)(void*),void* context){
    struct TLS {~TLS(){fake::event("probe TLS destroyed");}} tls;
    ++probe_fake::calls;probe_fake::entered=true;fake::wait(probe_fake::hold);
    return cancelled(context)?Response{false,-2010}:Response{probe_fake::ok,probe_fake::code};
  }
};
struct IPAddress {
  uint32_t value=0;int type()const{return 4;}
  unsigned operator[](unsigned i)const{return (value>>(24-8*i))&255;}
};
constexpr int IPv4=4;
struct esp_netif_t {};
struct esp_netif_ip_info_t {struct {uint32_t addr;}ip{},netmask{},gw{};};
inline esp_netif_t station;
inline int esp_netif_get_ip_info(esp_netif_t* p,esp_netif_ip_info_t* out){
  assert(p==&station);out->ip.addr=htonl(0x0a000002);out->netmask.addr=htonl(0xffffff00);out->gw.addr=htonl(0x0a000001);return 0;
}
struct ProbeWiFi {
  struct STAType {esp_netif_t* netif(){return probe_fake::have_netif?&station:nullptr;}} STA;
  int status(){return probe_fake::connected?WL_CONNECTED:0;}
  IPAddress dnsIP(unsigned i){return {i?0U:0x08080808U};}
};
inline ProbeWiFi WiFi;
