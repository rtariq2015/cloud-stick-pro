#pragma once
#include "Arduino.h"
class IPAddress {
 public:
  unsigned operator[](unsigned)const{return 255;}
  String toString()const{return "255.255.255.255";}
};
