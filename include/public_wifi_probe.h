#pragma once
#include "public_wifi_assistance.h"
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
#include "owned_guest_route.h"
namespace pro::public_wifi {
enum class ProbePhase:uint8_t {Stopped,Starting,Running,Drained,Failed};
struct ProbeSnapshot {ProbePhase phase=ProbePhase::Stopped;bool ok=false;int code=0;};
// One pinned owner; no automatic delete/reset. The probe's config/TLS scopes end
// before Drained, and owner verifies its suspended task before freeing buffers.
bool probeStart();
void probeCancel();
ProbeSnapshot probeSnapshot();
bool probeReap();
bool readUplink(pro::owned_guest::Uplink&);
}
#endif
