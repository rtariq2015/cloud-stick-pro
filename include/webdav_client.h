#pragma once
#include "pro_core.h"
#include "config_loader.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "remote_health_publisher.h"
class WebDAVClient:public pro::Remote {
 public:
  explicit WebDAVClient(const CloudConfig& config):config_(config){}
#if CSP_ENABLE_BACKUP_SETUP
  // Managed worker config is immutable until all client scopes have unwound.
  // Borrow it to avoid a second complete CA/credential allocation during TLS.
  WebDAVClient(CloudConfig&&)=delete;
  WebDAVClient(const CloudConfig&&)=delete;
#endif
  pro::Result put(const std::string&,uint64_t) override;
  pro::Result verify(const std::string&,const std::string&,uint64_t) override;
  pro::Result checkConnection();
  // Read-only reachability/authentication check for a paused owner coordinator.
  // Never creates collections, uploads files or touches storage/receipts. The
  // caller must keep config/context alive, stop BLE and exclude other TLS work.
  // Cancellation is independent of backup maintenance so backups stay paused.
  using ProbeCancelled=bool (*)(void*);
  pro::Result probeConnectionReadOnly(ProbeCancelled cancelled=nullptr,void* context=nullptr);
  pro::Result testConnection(char* testFile,size_t capacity);
#if CSP_ENABLE_REMOTE_HEALTH
  // Separate RAM-only metadata path. Does not touch SD, file receipts, backup
  // outcomes or connection-check state. No runtime hook is enabled yet.
  pro::health::Publication publishHealth(const pro::health::Snapshot&,const pro::health::Admission&);
#endif
 private:
#if CSP_ENABLE_BACKUP_SETUP
  const CloudConfig& config_;
#else
  CloudConfig config_;
#endif
  bool begin(HTTPClient&,WiFiClientSecure&,const String&);
  String url(const std::string&) const;
  std::string stagingDirectory() const;
  std::string stagedPath(const std::string&) const;
  pro::Result verifyURL(const String&,const std::string&,uint64_t,bool guarded);
  pro::Result destinationValidator(const std::string&,std::string& etag);
};
