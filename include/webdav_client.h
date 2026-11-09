#pragma once
#include "pro_core.h"
#include "config_loader.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "remote_health_publisher.h"
#ifndef CSP_ENABLE_DAV_COLLECTION_CACHE
#define CSP_ENABLE_DAV_COLLECTION_CACHE 0
#endif
#if CSP_ENABLE_DAV_COLLECTION_CACHE != 0 && CSP_ENABLE_DAV_COLLECTION_CACHE != 1
#error "DAV collection cache must be 0 or 1"
#endif
#if CSP_ENABLE_DAV_COLLECTION_CACHE && (!defined(CSP_PRODUCT_UPLOAD_DEV_PROFILE) || !CSP_PRODUCT_UPLOAD_DEV_PROFILE || !CSP_ENABLE_BACKUP_SETUP)
#error "DAV collection cache requires the separately identified managed upload candidate"
#endif
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
  pro::Result putFile(const std::string&,uint64_t);
  pro::Result verifyFile(const std::string&,const std::string&,uint64_t);
  pro::Result ensureParents(const std::string&);
#if CSP_ENABLE_DAV_COLLECTION_CACHE
  // Advisory, worker-lifetime metadata only. No hash collisions, file data,
  // credentials, receipts or persistent allocations. Every failed transfer
  // discards it; full PUT/readback/publication checks remain mandatory.
  char parent_[pro::PathLimit+1]={};
  bool stagingReady_=false;
  void forgetCollections(){parent_[0]='\0';stagingReady_=false;}
#endif
};
