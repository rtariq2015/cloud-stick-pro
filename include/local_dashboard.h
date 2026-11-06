#pragma once
#include <cstddef>
#include <cstdint>

// An additive, read-only LAN monitor. Configuration and private history remain
// on the authenticated HTTPS/Bluetooth interface. Earlier image identities and
// the default production environment do not link a listener.
#ifndef CSP_ENABLE_LOCAL_DASHBOARD
#define CSP_ENABLE_LOCAL_DASHBOARD 0
#endif
#ifndef CSP_PRODUCT_DASHBOARD_TRIAL
#define CSP_PRODUCT_DASHBOARD_TRIAL 0
#endif
// Source-only TCP close experiment. Production retains immediate close unless
// a distinct reviewed build explicitly selects the bounded passive-close path.
#ifndef CSP_DASHBOARD_PASSIVE_CLOSE
#define CSP_DASHBOARD_PASSIVE_CLOSE 0
#endif
#if CSP_DASHBOARD_PASSIVE_CLOSE != 0 && CSP_DASHBOARD_PASSIVE_CLOSE != 1
#error CSP_DASHBOARD_PASSIVE_CLOSE must be 0 or 1
#endif
#if CSP_PRODUCT_DASHBOARD_TRIAL != 0 && CSP_PRODUCT_DASHBOARD_TRIAL != 1
#error CSP_PRODUCT_DASHBOARD_TRIAL must be 0 or 1
#endif
#if CSP_ENABLE_LOCAL_DASHBOARD != 0 && CSP_ENABLE_LOCAL_DASHBOARD != 1
#error CSP_ENABLE_LOCAL_DASHBOARD must be 0 or 1
#endif
#if CSP_ENABLE_LOCAL_DASHBOARD && (!CSP_ENABLE_BACKUP_SETUP || !CSP_PRODUCT_DASHBOARD_TRIAL)
#error Local dashboard requires the distinct combined dashboard trial image
#endif
#if CSP_PRODUCT_DASHBOARD_TRIAL && !CSP_ENABLE_LOCAL_DASHBOARD
#error Dashboard trial identity requires its enabled read-only dashboard
#endif

namespace pro::local_dashboard {
enum class Route : uint8_t {BadRequest,Forbidden,Page,Script,Status};
constexpr size_t HeaderLimit=1536,StatusLimit=1024;
// Strict HTTP/1.1 GET dialect: exact station-IP Host, no body, query string,
// framing ambiguity, cross-origin JSON access or configuration routes.
Route route(const char*,size_t,const char* host);
struct Snapshot {
  const char* firmware="";
  uint32_t uptime=0,verified=0,pending=0,failed=0,speed=0,quiet=0;
  uint64_t done=0,total=0,last_backup=0;
  int error=0,percent=-1;
  uint8_t phase=0;
  bool wifi=false,media=false,active=false,scanning=false,has_scan=false,
       scan_complete=false,window_open=true,paused=false,cloud=false,configured=false;
};
size_t json(const Snapshot&,char*,size_t);
extern const char Page[],Script[];
extern const size_t PageSize,ScriptSize;
#if CSP_ENABLE_LOCAL_DASHBOARD
class Server {
 public:
  explicit Server(uint16_t port=80):port_(port){}
  ~Server();
  Server(const Server&)=delete;
  Server& operator=(const Server&)=delete;
  // Owner-loop only; never a new task, timer, SD operation or HTTP mutation.
  // Socket work is nonblocking and at most one bounded read/write per poll.
  void poll(uint32_t now,bool allowed,uint32_t network_address,
            bool memory_ok,const Snapshot&);
  bool stop();
  bool listening()const{return listener_>=0&&!cleanup_uncertain_;}
  bool serving()const{return client_>=0;}
  bool faulted()const{return cleanup_uncertain_;}
 private:
  bool closeClient();
  bool start(uint32_t);
  bool response(Route,const Snapshot&);
  int listener_=-1,client_=-1;
  uint16_t port_=80;
  uint32_t address_=0,accepted_=0,retry_at_=0;
  bool sending_=false,cleanup_uncertain_=false;
#if CSP_DASHBOARD_PASSIVE_CLOSE
  uint32_t finished_=0;
  bool waiting_for_peer_=false;
#endif
  size_t received_=0,header_size_=0,header_sent_=0,body_size_=0,body_sent_=0;
  const char* body_=nullptr;
  struct Buffers;
  Buffers* buffers_=nullptr;
  char host_[24]{};
};
static_assert(sizeof(Server)<=256,"Dashboard must not retain per-client working buffers while stopped");
void poll(uint32_t,bool,uint32_t,bool,const Snapshot&);
bool stop();
bool listening();
bool faulted();
#endif
}
