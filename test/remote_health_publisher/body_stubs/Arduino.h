#pragma once
#include "../../backup_integration/dav_stubs/Arduino.h"
#define log_v(...) ((void)0)
#define log_d(...) ((void)0)
#define log_w(...) ((void)0)
#define log_e(...) ((void)0)
inline void delay(unsigned n){fakeNow+=n;}
