#pragma once
#include <mutex>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL_SAFE(m) (m)->lock()
#define portEXIT_CRITICAL_SAFE(m) (m)->unlock()
