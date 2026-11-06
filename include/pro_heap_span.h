#pragma once
#include <cstddef>
#include <cstdint>
namespace pro::trace {
struct HeapBarrier {uintptr_t address=0;size_t bytes=0;};
struct HeapSpan {
  uintptr_t heap_start=0,heap_end=0,before_start=0,after_start=0,global_after_start=0;
  size_t before_bytes=0,after_bytes=0,global_after_bytes=0;
  uint32_t barriers=0,overflow=0;
  bool captured=false;
  HeapBarrier retained[16]{};
  void before(uintptr_t hs,uintptr_t he,uintptr_t address,size_t size,bool used){
    if(!used&&size>before_bytes){heap_start=hs;heap_end=he;before_start=address;before_bytes=size;}
  }
  void after(uintptr_t hs,uintptr_t he,uintptr_t address,size_t size,bool used){
    if(!used&&size>global_after_bytes){global_after_start=address;global_after_bytes=size;}
    if(hs!=heap_start||he!=heap_end||!before_bytes)return;
    // Raw free block intersecting the original largest span, including a newly
    // coalesced block starting before it. Integer differences avoid end overflow.
    const bool overlap=address>=before_start?address-before_start<before_bytes:
                       before_start-address<size;
    if(used&&overlap){
      if(barriers<16)retained[barriers++]={address,size};else ++overflow;
    }
    if(!used&&overlap&&size>after_bytes){after_start=address;after_bytes=size;}
  }
};
}
