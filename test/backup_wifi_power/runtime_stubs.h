#pragma once
#include <cstddef>
size_t backupPowerRecordedHeap();
#define runtimeFreeHeap backupPowerRecordedHeap()
#include "../backup_integration/runtime_stubs.h"
#undef runtimeFreeHeap
