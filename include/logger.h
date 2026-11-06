#pragma once
#include <Arduino.h>
void log_init();
// Bounded serial diagnostics only. Never log credentials.
void stick_log_printf(const char* format, ...);
