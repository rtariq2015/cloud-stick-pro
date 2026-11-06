#pragma once
#include "remote_health_model.h"
#if CSP_ENABLE_REMOTE_HEALTH
namespace pro::health {
// Internal native admission only, never populated from a phone/network request.
// A future owner/worker bridge must prove each fact. Defaults deny all I/O.
struct Admission {bool normal_worker=false,sd_view_unmounted=false,radio_exclusive=false;};
struct Publication {bool ok=false;int32_t code=0;uint64_t revision=0;uint8_t slot=0;};
constexpr int32_t HealthDenied=-2400,HealthInvalid=-2401,HealthCollision=-2402,
                  HealthMalformed=-2403,HealthDeadline=-2404;
}
#endif
