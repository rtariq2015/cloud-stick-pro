#pragma once
// Use the existing synthetic NVS/USB/radio fixtures with the tested Arduino
// String adapter so the actual config loader and codec can link in this suite.
#define String RuntimeBaselineString
#include "../backup_integration/runtime_stubs.h"
#undef String
#include "../backup_integration/config_stubs/Arduino.h"
