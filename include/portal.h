#pragma once
#include <Arduino.h>
bool portal_start();
void portal_poll();
bool portal_active();
bool portal_expired();
const String& portal_password();
const String& portal_ssid();

void dashboard_poll();
