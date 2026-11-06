#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../src/panic_diagnostics.cpp"

namespace {
arduino_panic_handler_t registered=nullptr;
arduino_panic_handler_t& _panic_handler=registered;
void* registered_arg=nullptr;
unsigned registrations=0,original_calls=0,walker_calls=0;
esp_reset_reason_t reset_reason=ESP_RST_PANIC;
struct panic_info_t {arduino_panic_info_t* fixture;};
void handle_custom_backtrace(panic_info_t* info){++walker_calls;registered(info->fixture,registered_arg);}
}
esp_reset_reason_t esp_reset_reason(){return reset_reason;}
void set_arduino_panic_handler(arduino_panic_handler_t handler,void* arg){registered=handler;registered_arg=arg;++registrations;}
extern "C" void __real_esp_panic_handler(panic_info_t*){++original_calls;}
// The wrapper body is extracted verbatim; the architecture walker above is a
// synthetic producer, not a claim that native tests execute target unwinding.
#include "pinned_wrapper.h"

int main(int argc,char** argv){
  assert(argc==2);const std::string mode=argv[1];
  panic_diagnostics_begin();assert(registrations==1&&registered&&registered_arg==nullptr);
  assert(retained.magic==0);
  arduino_panic_info_t info{};info.core=1;info.pc=reinterpret_cast<const void*>(uintptr_t(UINT32_MAX));
  info.reason="SYNTHETIC_PRIVATE_REASON_MUST_NEVER_APPEAR";
  info.backtrace_len=16;for(auto& pc:info.backtrace)pc=UINT32_MAX;
  if(mode=="empty")info.backtrace_len=0;
  if(mode=="sixty")info.backtrace_len=60;
  if(mode=="oversized")info.backtrace_len=UINT_MAX;
  if(mode=="continues")info.backtrace_continues=true;
  if(mode=="corrupt-trace")info.backtrace_corrupt=true;
  if(mode=="invalid-core")info.core=2;
  if(mode=="negative-core")info.core=-1;
  panic_info_t abstract{mode=="null"?nullptr:&info};
  __wrap_esp_panic_handler(&abstract);
  assert(walker_calls==1&&original_calls==1); // Original SDK handler is not replaced.
  if(mode=="bad-magic")retained.magic^=1;
  if(mode=="partial")retained.magic=0;
  if(mode=="bad-checksum")retained.checksum^=1;
  if(mode=="bad-version"){retained.version=2;retained.checksum=checksum();}
  if(mode=="bad-count"){retained.count=17;retained.checksum=checksum();}
  if(mode=="bad-core"){retained.core=2;retained.checksum=checksum();}
  if(mode=="bad-flags"){retained.corrupt=2;retained.checksum=checksum();}
  if(mode=="power-on")reset_reason=ESP_RST_POWERON;
  if(mode=="software-reset")reset_reason=ESP_RST_SW;
  if(mode=="watchdog-reset")reset_reason=ESP_RST_TASK_WDT;
  initialized=false;boot_valid=false;boot_record={};
  panic_diagnostics_begin();assert(retained.magic==0&&registrations==2);
  const bool expected=mode=="maximum"||mode=="empty"||mode=="sixty"||mode=="oversized"||mode=="continues"||mode=="corrupt-trace"||mode=="idempotent"||mode=="freshness"||mode=="format-bounds";
  assert(boot_valid==expected);
  if(mode=="idempotent"){panic_diagnostics_begin();assert(registrations==2&&boot_valid);}
  if(mode=="freshness"){
    // A second panic before its walker/callback succeeds cannot reuse this one.
    initialized=false;boot_valid=false;boot_record={};panic_diagnostics_begin();assert(!boot_valid);
  }
  const auto retained_magic=retained.magic,retained_checksum=retained.checksum;
  char out[PanicDiagnosticJsonLimit];assert(panic_diagnostics_json(out,sizeof out));
  assert(retained.magic==retained_magic&&retained.checksum==retained_checksum);
  assert(std::strlen(out)<PanicDiagnosticJsonLimit&&std::strstr(out,"SYNTHETIC")==nullptr);
  assert(std::strstr(out,"\"reason\":")==nullptr); // No free-form reason field.
  if(mode=="format-bounds"){
    assert(!panic_diagnostics_json(nullptr,10));assert(!panic_diagnostics_json(out,0));
    char tiny[3]={'x','y','z'};assert(!panic_diagnostics_json(tiny,1));assert(tiny[0]==0&&tiny[1]=='y'&&tiny[2]=='z');
    char short_out[80];std::memset(short_out,'x',sizeof short_out);assert(!panic_diagnostics_json(short_out,sizeof short_out));assert(short_out[0]==0);
    assert(panic_diagnostics_json(out,sizeof out));
  }
  std::puts(out);
}
