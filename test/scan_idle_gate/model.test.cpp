#include "scan_idle_gate.h"
#include <cassert>
#include <iostream>
#include <limits>
using pro::scan::IdleGate;
using pro::scan::Summary;
const std::string Scope(64,'a'),Metadata(64,'b');
Summary clean(){return {7,0,0,0,2,1767225605,true,true,0};}
void baseline(IdleGate& gate,uint64_t generation=9,uint32_t now=10){
  assert(gate.completed(generation,generation,Scope,Metadata,now,clean(),7,true,true));
}
int main(){
  IdleGate gate;
  assert(!gate.skip(9,Scope,11,clean(),true)); // Every boot starts unknown.
  baseline(gate);
  for(unsigned i=0;i<100000;++i)assert(gate.skip(9,Scope,11+i,clean(),true));
  assert(!gate.skip(10,Scope,100012,clean(),true)); // Same-size edits still dirty.
  assert(!gate.valid());
  baseline(gate);assert(!gate.completed(9,10,Scope,Metadata,10,clean(),7,true,true));
  assert(!gate.valid()); // A host write interleaved with scan cannot clear dirty.
  for(unsigned fault=0;fault<9;++fault){
    auto value=clean();baseline(gate);
    if(fault==0)value.pending=1;
    if(fault==1)value.failed=1;
    if(fault==2)value.skipped=1;
    if(fault==3)value.error=-1;
    if(fault==4)value.hasScan=false;
    if(fault==5)value.complete=false;
    assert(!gate.completed(9,9,Scope,fault==6?"corrupt":Metadata,10,value,
                           fault==7?8:7,fault!=8,true));
    assert(!gate.skip(9,Scope,11,value,true));
  }
  baseline(gate);gate.resume(9,Scope,Metadata,clean(),true);
  assert(gate.skip(9,Scope,12,clean(),true)); // Same-boot worker restart.
  baseline(gate);gate.resume(9,std::string(64,'c'),Metadata,clean(),true);assert(!gate.valid());
  baseline(gate);gate.resume(9,Scope,std::string(64,'d'),clean(),true);assert(!gate.valid());
  baseline(gate);gate.resume(9,Scope,Metadata,Summary{},true);assert(!gate.valid());
  baseline(gate);assert(!gate.skip(9,Scope,12,clean(),false)); // Media fault.
  baseline(gate,9,UINT32_MAX-1000u);
  assert(gate.skip(9,Scope,999,clean(),true)); // Millis wrap, 2 seconds elapsed.
  assert(!gate.skip(9,Scope,uint32_t(UINT32_MAX-1000u+IdleGate::AuditMs),clean(),true));
  baseline(gate);gate.invalidate();assert(!gate.valid());
  assert(sizeof(IdleGate)<=256);
  std::cout<<"PASS model: boot/100000 idle cycles/epoch race/errors/pending/corrupt/restart/scope/status/media/24h wrap/invalidation; RAM="<<sizeof(IdleGate)<<" bytes\n";
}
