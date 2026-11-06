#pragma once
#include <cassert>
// Source-only integrated owner loop: retain the existing synthetic radio/USB/
// NVS fixtures and expose only a station address and dashboard lifecycle.
#define FakeWiFi IntegratedBaselineWiFi
#define WiFi unusedIntegratedBaselineWiFi
#include "../cloud_setup/runtime_stubs.h"
#undef WiFi
#undef FakeWiFi
struct IntegratedFixtureIP {
  operator uint32_t() const { return 0x0100007fu; }
  std::string toString() const { return "127.0.0.1"; }
};
struct FakeWiFi : IntegratedBaselineWiFi {
  IntegratedFixtureIP localIP() const { return {}; }
};
extern FakeWiFi WiFi;
#include "local_dashboard.h"
extern bool bleRunning;
namespace integrated_dashboard_fixture {
inline bool open=false;
inline unsigned polls=0,stops=0;
}
namespace pro::local_dashboard {
inline bool stop() { ++integrated_dashboard_fixture::stops;integrated_dashboard_fixture::open=false;return true; }
inline bool listening() { return integrated_dashboard_fixture::open; }
inline bool faulted() { return false; }
inline void poll(uint32_t,bool allowed,uint32_t address,bool memory_ok,const Snapshot&) {
  ++integrated_dashboard_fixture::polls;
  if(allowed&&address&&memory_ok) { assert(!bleRunning);integrated_dashboard_fixture::open=true; }
  else stop();
}
}
