#pragma once
#include "WiFi.h"
#ifndef CSP_ENABLE_LOCAL_DASHBOARD
#define CSP_ENABLE_LOCAL_DASHBOARD 1
#endif
namespace pro::local_dashboard {
inline bool listening(){return true;}
}
