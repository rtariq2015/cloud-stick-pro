#pragma once
#include "common.h"
void diagnostics_init(bool);
void diagnostics_poll();
void diagnostics_add_status(JsonDocument&);
String diagnostics_json(bool log=false);
