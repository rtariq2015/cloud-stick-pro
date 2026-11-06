#pragma once
#include <cstddef>
#include <cstdint>
// Keep the existing synthetic owner-loop fixtures, with only an observable heap
// sample and a public station-IP accessor added for the dashboard boundary.
// Renaming the original Wi-Fi declaration avoids changing shared test fixtures.
size_t dashboardRecordedHeap();
#define FakeWiFi DashboardBaselineWiFi
#define WiFi unusedDashboardBaselineWiFi
#define runtimeFreeHeap dashboardRecordedHeap()
#include "../backup_integration/runtime_stubs.h"
#undef runtimeFreeHeap
#undef WiFi
#undef FakeWiFi
struct DashboardFixtureIP {
  operator uint32_t()const{return 0x0100007fu;}
  std::string toString()const{return "127.0.0.1";}
};
struct FakeWiFi : DashboardBaselineWiFi {
  DashboardFixtureIP localIP() { return {}; }
};
extern FakeWiFi WiFi;
