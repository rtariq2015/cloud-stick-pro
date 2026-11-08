// SPDX-License-Identifier: MIT
#include "ble_release_trace.h"
#include <esp_heap_caps.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
namespace {unsigned walks=0;void* retained=nullptr;}
extern "C" __attribute__((noinline)) void* heap_caps_malloc(size_t n,uint32_t){return std::malloc(n);}
extern "C" __attribute__((noinline)) void* heap_caps_calloc(size_t n,size_t size,uint32_t){return std::calloc(n,size);}
extern "C" __attribute__((noinline)) void heap_caps_free(void* p){std::free(p);}
void heap_caps_walk(uint32_t caps,heap_caps_walker_cb_t callback,void* context){
  assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  // Intentionally invalid payload addresses: an observer that dereferenced a
  // heap block instead of metadata would fail this sanitizer run.
  walker_heap_into_t heap{1,INTPTR_MAX};
  if(++walks%2){assert(callback(heap,{reinterpret_cast<void*>(1),size_t(INTPTR_MAX-1),false},context));}
  else{assert(callback(heap,{reinterpret_cast<void*>(2),128,true},context));assert(callback(heap,{retained,128,true},context));}
}
int main(){
  void* before=std::malloc(128);assert(before);
  ble_release_trace_begin();
  retained=heap_caps_calloc(1,128,MALLOC_CAP_INTERNAL);assert(retained);std::memset(retained,0x5a,128);
  void* temporary=heap_caps_malloc(64,MALLOC_CAP_INTERNAL);assert(temporary);const auto temporary_address=reinterpret_cast<uintptr_t>(temporary);heap_caps_free(temporary);
  ble_release_trace_finish();const auto span=ble_release_trace_span(),again=ble_release_trace_span();
  assert(span.captured&&span.barriers==2&&again.barriers==2);
  pro::trace::Record rows[128];const size_t count=ble_release_trace_page(0,rows,128);
  bool found=false;for(size_t i=0;i<count;++i){assert(rows[i].address!=temporary_address);if(rows[i].address==reinterpret_cast<uintptr_t>(retained)){assert(rows[i].live&&rows[i].bytes==128&&rows[i].caller);found=true;}}
  assert(found&&ble_release_trace_summary().overflow==0);
  std::free(before);heap_caps_free(retained);
  ble_release_trace_begin();ble_release_trace_finish();assert(ble_release_trace_page(0,rows,128)==0);
  std::cout<<"PASS actual release observer/wrappers; synthetic heap/RTOS, no payload access, no device\n";
}
