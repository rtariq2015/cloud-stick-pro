#pragma once
#include "pro_core.h"
#include "ble_feature.h"
#ifndef CSP_ENABLE_CLOUD_REQUEST_TRACE
#define CSP_ENABLE_CLOUD_REQUEST_TRACE 0
#endif
#if CSP_ENABLE_CLOUD_REQUEST_TRACE != 0 && CSP_ENABLE_CLOUD_REQUEST_TRACE != 1
#error "Cloud request trace must be 0 or 1"
#endif
#if CSP_ENABLE_CLOUD_REQUEST_TRACE && (!CSP_ENABLE_BACKUP_SETUP || !defined(CSP_PRODUCT_UPLOAD_DEV_PROFILE) || !CSP_PRODUCT_UPLOAD_DEV_PROFILE)
#error "Cloud request trace requires the separately identified upload candidate"
#endif
#ifndef CSP_ENABLE_CLOUD_TLS_TRACE
#define CSP_ENABLE_CLOUD_TLS_TRACE 0
#endif
#if CSP_ENABLE_CLOUD_TLS_TRACE != 0 && CSP_ENABLE_CLOUD_TLS_TRACE != 1
#error "Cloud TLS trace must be 0 or 1"
#endif
#if CSP_ENABLE_CLOUD_TLS_TRACE && !CSP_ENABLE_CLOUD_REQUEST_TRACE
#error "Cloud TLS trace requires the cloud request trace"
#endif
// Only constant operation identifiers and numeric outcomes cross this boundary.
// No URL, filename, account, header, body, key or pointer is retained.
enum class CloudRequestOp:uint8_t {
  None,ParentFolder,StagingFolder,StagePut,StageReadback,DestinationType,
  Publish,FinalReadback,ReplacementPut,Cleanup,FolderCheck,RootFolder
};
constexpr int32_t CloudBodyNotRead=INT32_MIN;
struct CloudRequestEvent {
  uint32_t id=0,elapsed_ms=0;
  int32_t http_code=CloudBodyNotRead,body_result=CloudBodyNotRead,result_code=CloudBodyNotRead;
  int32_t tls_error=0;
  CloudRequestOp operation=CloudRequestOp::None;
};
#if CSP_ENABLE_CLOUD_TLS_TRACE
// Called only for an unchanged negative return from the real TLS reader.
// The active request's task must match; no context or payload pointer crosses.
void cloud_request_tls_error(int32_t);
#endif
struct CloudRequestSnapshot {
  CloudRequestEvent current,last,last_negative;
  uint32_t attempts=0;
  bool enabled=false,active=false;
};
#if CSP_ENABLE_CLOUD_REQUEST_TRACE
CloudRequestSnapshot cloud_request_snapshot();
class CloudRequestTrace {
  CloudRequestEvent event_;
  uint32_t started_=0;
  bool finished_=false;
 public:
  explicit CloudRequestTrace(CloudRequestOp);
  ~CloudRequestTrace();
  CloudRequestTrace(const CloudRequestTrace&)=delete;
  CloudRequestTrace& operator=(const CloudRequestTrace&)=delete;
  void headers(int32_t code){event_.http_code=code;}
  void body(int32_t result){event_.body_result=result;}
  // Return exactly the original result. Observations never change decisions,
  // trigger I/O/retries, save receipts, or alter any transport/timeout setting.
  pro::Result finish(pro::Result);
};
#else
inline CloudRequestSnapshot cloud_request_snapshot(){return {};}
class CloudRequestTrace {
 public:
  explicit CloudRequestTrace(CloudRequestOp){}
  void headers(int32_t){}void body(int32_t){}
  pro::Result finish(pro::Result result){return result;}
};
#endif
