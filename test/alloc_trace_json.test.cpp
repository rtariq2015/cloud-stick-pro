// Real isolated JSON exporter with deterministic recorder data. No device I/O.
#include "ble_alloc_trace.h"
#include <cassert>
#include <set>
#include <iostream>
#include <limits>
static pro::trace::Tracker<256> fixture;
void ble_alloc_trace_begin(){fixture.begin();}
void ble_alloc_trace_phase(pro::trace::Phase p){fixture.phase(p);}
void ble_alloc_trace_stop(uint32_t){fixture.stop();}
void ble_alloc_trace_poll(uint32_t){fixture.freeze();}
pro::trace::Summary ble_alloc_trace_summary(){return fixture.summary();}
size_t ble_alloc_trace_page(size_t n,pro::trace::Record* p,size_t c){return fixture.page(n,p,c);}
BleDeniedCall ble_alloc_trace_denied_call(){return {0xffffffff,7,3};}
void ble_alloc_trace_denial(uint32_t,uintptr_t){}
pro::trace::HeapSpan ble_alloc_trace_heap_span(){
 pro::trace::HeapSpan s;s.captured=true;s.heap_start=0x3fc90000;s.heap_end=0x3fcd0000;
 s.before_start=0x3fc91000;s.before_bytes=0x30000;s.after_start=0x3fc99000;
 s.after_bytes=0x28000;s.global_after_start=s.after_start;s.global_after_bytes=s.after_bytes;
 s.barriers=16;s.overflow=UINT32_MAX;
 for(auto& b:s.retained){b.address=UINT32_MAX;b.bytes=UINT32_MAX;}return s;
}
#define main baseline_isolated_test_main
#include "milestone1_corrections/isolated_boot_runtime.cpp"
#undef main

int main(){
 setup();tick(1000);command("STATUS");
 for(unsigned i=0;i<100;++i)events.add({1,UINT32_MAX,0,255,-2147483647});
 fixture.begin();fixture.phase(pro::trace::Phase::Authenticated);
 for(uint32_t i=0;i<256;++i)fixture.allocation(0x3fc00000+i*16,UINT32_MAX,0x42000000+i*8,pro::trace::Api::Aligned);
 // LOG does not expose a moving set of allocation records.
 JsonDocument active;assert(!deserializeJson(active,diagnostics_json(true)));
 assert(active["alloc_trace"]["phase"].as<unsigned>()==3);
 assert(active["alloc_trace"]["records"].isNull());
 fixture.stop();fixture.release(0x3fc00000);fixture.freeze();
 std::set<uint32_t> addresses;size_t maxBytes=0;
 for(unsigned page=0;page<22;++page){
  const String reply=diagnostics_json(true);maxBytes=std::max(maxBytes,reply.size());assert(reply.size()<8192);
  JsonDocument d;assert(!deserializeJson(d,reply));auto t=d["alloc_trace"];
  assert(t["snapshot"].as<unsigned>()==1&&t["phase"].as<unsigned>()==5);
  assert(t["page"].as<unsigned>()==page&&t["pages"].as<unsigned>()==22);
  assert(t["at_stop"].as<unsigned>()==256&&t["still_live"].as<unsigned>()==255);
  assert(t["released_later"].as<unsigned>()==1&&t["overflow"].as<unsigned>()==0);
  for(auto row:t["records"].as<JsonArray>()){
   assert(row.size()==7);assert(addresses.insert(row["address"].as<uint32_t>()).second);
   assert(row["caller"].as<uint32_t>()>=0x42000000&&row["bytes"].as<uint64_t>()==UINT32_MAX);
  }
  JsonDocument status;assert(!deserializeJson(status,diagnostics_json(false)));
  assert(status["alloc_trace"]["page"].isNull()); // STATUS does not advance LOG pagination
 }
 assert(addresses.size()==256);
 JsonDocument cycle;assert(!deserializeJson(cycle,diagnostics_json(true)));
 assert(cycle["alloc_trace"]["page"].as<unsigned>()==0);
 fixture.begin();fixture.stop();fixture.freeze();
 JsonDocument empty;assert(!deserializeJson(empty,diagnostics_json(true)));
 assert(empty["alloc_trace"]["snapshot"].as<unsigned>()==2);
 assert(empty["alloc_trace"]["page"].as<unsigned>()==0&&empty["alloc_trace"]["pages"].as<unsigned>()==1);
 assert(empty["alloc_trace"]["records"].size()==0);
 assertContained();
 std::cout<<"PASS actual JSON: 256 records across 22 immutable pages, empty/new snapshots, STATUS cursor isolation; largest reply "<<maxBytes<<" bytes (<8192)\n";
}
