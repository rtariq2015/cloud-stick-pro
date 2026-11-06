#pragma once
#include "Arduino.h"
namespace lcdmock {inline bool media=true;}
inline unsigned storage_quiet_remaining(unsigned){return lcdmock::quiet;}
inline bool storage_media_detected(){return lcdmock::media;}
