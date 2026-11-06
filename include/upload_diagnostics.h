#pragma once
#include <cstddef>
#include <cstdint>
#include "ble_feature.h"
#ifndef CSP_UPLOAD_DIAGNOSTICS
#define CSP_UPLOAD_DIAGNOSTICS 0
#endif
#if CSP_UPLOAD_DIAGNOSTICS != 0 && CSP_UPLOAD_DIAGNOSTICS != 1
#error CSP_UPLOAD_DIAGNOSTICS must be zero or one
#endif
#if CSP_UPLOAD_DIAGNOSTICS && (!CSP_ENABLE_BACKUP_SETUP || !CSP_USB_BENCH_AUTOMATION)
#error Upload diagnostics require the explicit combined USB bench profile
#endif
enum class UploadDiagnosticTarget:uint8_t {Stage=1,Replacement=2};
enum class UploadSourceAbort:uint8_t {None=0,Unavailable=1,ReadError=2,EarlyEof=3,Deadline=4};
// Numeric RAM observations only. TLS application totals include the request
// header; net totals also include encrypted record/handshake overhead. Neither
// total is a claim that the server accepted or verified a file.
struct UploadDiagnosticSnapshot {
  uint64_t expected_bytes=0,source_fread_bytes=0,source_returned_bytes=0;
  uint64_t tls_requested_bytes=0,tls_accepted_bytes=0,net_requested_bytes=0,net_accepted_bytes=0;
  uint32_t attempt=0,request_ms=0,socket_timeout_ms=0;
  uint32_t source_calls=0,short_reads=0,max_read_ms=0;
  uint32_t tls_write_calls=0,tls_read_calls=0,net_send_calls=0;
  uint32_t want_read=0,want_write=0,max_tls_write_ms=0,last_positive_age_ms=0;
  int32_t http_code=0,source_errno=0,tls_last_write_ret=0,tls_first_fatal_write=0;
  int32_t tls_last_read_ret=0,tls_first_fatal_read=0,net_last_ret=0,net_first_fatal=0,net_errno=0;
  int32_t tls_write_errno=0,tls_read_errno=0;
  uint8_t target=0,source_abort=0;
  bool enabled=false,active=false,completed=false,source_ferror=false,source_eof=false;
};
#if CSP_UPLOAD_DIAGNOSTICS
UploadDiagnosticSnapshot upload_diagnostics_snapshot();
void upload_diagnostics_source_abort(UploadSourceAbort);
void upload_diagnostics_source_read(size_t requested,size_t got,uint32_t elapsed_ms,
                                   int32_t read_errno,bool read_error,bool eof,bool returned);
// PUT scopes belong to one existing worker task. Other task TLS activity is
// excluded. The scope never initiates I/O, changes a result or marks a receipt.
class UploadDiagnosticAttempt {
  uint32_t attempt_=0;
 public:
  UploadDiagnosticAttempt(uint64_t expected,UploadDiagnosticTarget);
  ~UploadDiagnosticAttempt();
  UploadDiagnosticAttempt(const UploadDiagnosticAttempt&)=delete;
  UploadDiagnosticAttempt& operator=(const UploadDiagnosticAttempt&)=delete;
  void finish(int32_t http_code,uint32_t socket_timeout_ms,bool read_error,bool eof);
};
#else
inline UploadDiagnosticSnapshot upload_diagnostics_snapshot(){UploadDiagnosticSnapshot s;return s;}
#endif
