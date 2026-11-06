#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
void diagnostics_init(bool hasConfig);
void diagnostics_poll();
void diagnostics_add_status(JsonDocument&);
String diagnostics_json(bool log=false);
void request_setup();
void restart_after_eject();
void resume_saved_backups();
