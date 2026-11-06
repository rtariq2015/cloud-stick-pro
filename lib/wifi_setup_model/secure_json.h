#pragma once
#include "wifi_setup_model.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
namespace pro::wifi_setup {
// Per-document allocation budget and best-effort erasure of this parser's own
// heap blocks. SDK/driver/browser copies have independent lifetimes.
class ScrubJsonAllocator final:public ArduinoJson::Allocator {
  struct alignas(std::max_align_t) Header {size_t size;};
  size_t used_=0;
 public:
  static constexpr size_t Limit=8192;
  void* allocate(size_t n) override {
    if(!n||n>Limit-used_)return nullptr;
    auto* h=static_cast<Header*>(std::malloc(sizeof(Header)+n));if(!h)return nullptr;
    h->size=n;used_+=n;return h+1;
  }
  void deallocate(void* p) override {
    if(!p)return;auto* h=static_cast<Header*>(p)-1;used_-=h->size;
    eraseSecretMemory(p,h->size);eraseSecretMemory(h,sizeof(Header));std::free(h);
  }
  void* reallocate(void* p,size_t n) override {
    if(!p)return allocate(n);if(!n){deallocate(p);return nullptr;}
    const size_t old=(static_cast<Header*>(p)-1)->size;
    // ArduinoJson's StringBuilder requires shrinking a live allocation to
    // succeed. Retain its physical capacity/accounting instead of allocating a
    // second block at peak budget; erase the discarded tail immediately.
    if(n<=old){eraseSecretMemory(static_cast<uint8_t*>(p)+n,old-n);return p;}
    void* next=allocate(n);if(!next)return nullptr;
    std::memcpy(next,p,std::min(old,n));deallocate(p);return next;
  }
};
} // namespace pro::wifi_setup
