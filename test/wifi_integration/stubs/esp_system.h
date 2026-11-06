#pragma once
#include "common.h"
// Desktop fixture only; actual target uses the pinned ESP-IDF reset reason.
inline int esp_reset_reason(){return 4;}
