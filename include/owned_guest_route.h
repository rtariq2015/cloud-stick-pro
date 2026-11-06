#pragma once
#ifndef CSP_ENABLE_OWNED_GUEST_ROUTE
#define CSP_ENABLE_OWNED_GUEST_ROUTE 0
#endif
#if CSP_ENABLE_OWNED_GUEST_ROUTE != 0 && CSP_ENABLE_OWNED_GUEST_ROUTE != 1
#error CSP_ENABLE_OWNED_GUEST_ROUTE must be zero or one
#endif
#if CSP_ENABLE_OWNED_GUEST_ROUTE
#include "route_diagnostics.h"
#include <cstdint>
#include <string_view>
namespace pro::owned_guest {
// Trusted owner-loop backend only: caller proves private ownership, a live grant,
// retired backup/TLS work, BLE shutdown and memory admission. No HTTP/auth API.
// Every IPv4 integer below uses host/display order (192.168.1.2 = 0xc0a80102).
struct Uplink {uint32_t ip=0,mask=0,gateway=0,dns=0;};
struct Request {std::string_view ap_name,ap_password;Uplink uplink;uint32_t window_ms=300000;};
enum class Phase:uint8_t {Stopped,Starting,Active,Stopping,Blocked};
enum class Error:uint8_t {None,InvalidRequest,Owner,UplinkChanged,NoSubnet,Allocation,
  EventRegister,EventBarrier,ApStart,ApConfigure,ApProof,Socket,EnableNapt,
  DisableNapt,CloseSocket,ApStop,EventUnregister,Expired,Deadline};
struct Status {
  Phase phase=Phase::Stopped;Error error=Error::None;int native_error=0;
  uint32_t generation=0,ap_ip=0,remaining_ms=0,queries=0,replies=0;
  bool station_preserved=false,resources_retained=false;
};
// Stages RAM input; poll drives startup and cleanup. A false result can require
// cleanup: always inspect status and poll Starting/Stopping until settled.
bool start(const Request&);
void poll(uint32_t now);
bool stop(); // Asynchronous; true only when cleanup reached Stopped.
Status status(); // Owner only; never includes station/AP passwords or URLs.
#if CSP_PRODUCT_LAB6_PROFILE
struct Diagnostic {uint8_t failure_phase=0,failure_step=0,ap_proof=0;};
Diagnostic diagnostic(); // Numeric RAM observations only, same owner restriction.
#endif
} // namespace pro::owned_guest
#endif
