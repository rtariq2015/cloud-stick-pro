#pragma once
#include <cstddef>
// Legacy save/reset functions are linked for the real config loader but never
// invoked by the owner-cloud runtime. Unexpected use cannot mutate the fixture.
class Preferences {
 public:
  bool begin(const char*,bool){return false;}
  size_t putBytes(const char*,const void*,size_t){return 0;}
  void end(){}
  bool remove(const char*){return false;}
};
