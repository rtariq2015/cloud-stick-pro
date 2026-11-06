#pragma once
#include "nvs.h"
class Preferences {
 public:
  bool begin(const char*,bool readonly){if(!readonly)++config_fake::writes;return false;}
  size_t putBytes(const char*,const void*,size_t){++config_fake::writes;return 0;}
  bool remove(const char*){++config_fake::erases;return false;}
  void end(){}
};
