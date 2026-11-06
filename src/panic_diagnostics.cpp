#include "panic_diagnostics.h"
#if CSP_PANIC_DIAGNOSTICS
#include <esp_attr.h>
#include <esp_system.h>
#include <esp32-hal.h>
#include <cstdint>
#include <cstdio>

#if !CONFIG_IDF_TARGET_ESP32S3
#error Revalidate the pinned panic callback before changing target
#endif
static_assert(sizeof(unsigned int)==4,"Pinned processed PCs are 32-bit");
static_assert(sizeof(((arduino_panic_info_t*)nullptr)->backtrace)==60*sizeof(unsigned int),
              "Revalidate the pinned Arduino panic callback ABI");
namespace {
constexpr uint32_t Magic=0x43535050u,Version=1;
struct RetainedPanic {
  uint32_t magic,version,core,pc,count,corrupt,truncated;
  uint32_t pcs[PanicDiagnosticPcLimit];
  uint32_t checksum;
};
static_assert(sizeof(RetainedPanic)==96,"Keep panic evidence bounded");
// No initializer: retained across a soft panic reboot, never persisted to flash.
RTC_NOINIT_ATTR volatile RetainedPanic retained;
RetainedPanic boot_record{};
uint32_t boot_reset_reason=0;
bool boot_valid=false,initialized=false;

__attribute__((always_inline)) inline uint32_t checksum(){
  uint32_t value=2166136261u;
  value=(value^retained.version)*16777619u;
  value=(value^retained.core)*16777619u;
  value=(value^retained.pc)*16777619u;
  value=(value^retained.count)*16777619u;
  value=(value^retained.corrupt)*16777619u;
  value=(value^retained.truncated)*16777619u;
  for(size_t i=0;i<PanicDiagnosticPcLimit;++i)value=(value^retained.pcs[i])*16777619u;
  return value;
}
}

// Called by the pinned Arduino wrapper after its official processed-PC walk.
// No strings, allocation, locks, I/O, raw stack/register dump, or reset command.
extern "C" void IRAM_ATTR csp_lab_record_panic(arduino_panic_info_t* info,void*){
  retained.magic=0;
  if(!info||info->core<0||info->core>1)return;
  retained.version=Version;retained.core=static_cast<uint32_t>(info->core);
  retained.pc=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->pc));
  const size_t count=info->backtrace_len<PanicDiagnosticPcLimit?info->backtrace_len:PanicDiagnosticPcLimit;
  retained.count=static_cast<uint32_t>(count);
  retained.corrupt=info->backtrace_corrupt?1u:0u;
  retained.truncated=info->backtrace_continues||info->backtrace_len>PanicDiagnosticPcLimit?1u:0u;
  for(size_t i=0;i<PanicDiagnosticPcLimit;++i)retained.pcs[i]=i<count?info->backtrace[i]:0;
  retained.checksum=checksum();
  // Complete retained payload stores before publishing the commit word.
#if defined(__XTENSA__)
  __asm__ __volatile__("memw" ::: "memory");
#else
  __asm__ __volatile__("" ::: "memory");
#endif
  retained.magic=Magic;
}

void panic_diagnostics_begin(){
  if(initialized)return;
  initialized=true;boot_reset_reason=static_cast<uint32_t>(esp_reset_reason());
  boot_valid=boot_reset_reason==ESP_RST_PANIC&&retained.magic==Magic&&
    retained.version==Version&&retained.core<2&&retained.count<=PanicDiagnosticPcLimit&&
    retained.corrupt<=1&&retained.truncated<=1&&retained.checksum==checksum();
  if(boot_valid){
    boot_record.version=retained.version;boot_record.core=retained.core;boot_record.pc=retained.pc;
    boot_record.count=retained.count;boot_record.corrupt=retained.corrupt;boot_record.truncated=retained.truncated;
    for(size_t i=0;i<PanicDiagnosticPcLimit;++i)boot_record.pcs[i]=retained.pcs[i];
  }
  // A later failure before the callback must not reuse an older crash record.
  retained.magic=0;
  set_arduino_panic_handler(csp_lab_record_panic,nullptr);
}

bool panic_diagnostics_json(char* out,size_t capacity){
  if(!out||!capacity)return false;
  out[0]=0;
  int n=std::snprintf(out,capacity,
    "{\"panic_valid\":%s,\"reset_reason\":%lu,\"core\":%lu,\"pc\":%lu,\"count\":%lu,\"backtrace_corrupt\":%s,\"backtrace_truncated\":%s,\"pcs\":[",
    boot_valid?"true":"false",static_cast<unsigned long>(boot_reset_reason),
    static_cast<unsigned long>(boot_valid?boot_record.core:0),static_cast<unsigned long>(boot_valid?boot_record.pc:0),
    static_cast<unsigned long>(boot_valid?boot_record.count:0),
    boot_valid&&boot_record.corrupt?"true":"false",boot_valid&&boot_record.truncated?"true":"false");
  if(n<0||static_cast<size_t>(n)>=capacity){out[0]=0;return false;}
  size_t used=static_cast<size_t>(n);
  for(size_t i=0;boot_valid&&i<boot_record.count;++i){
    n=std::snprintf(out+used,capacity-used,"%s%lu",i?",":"",static_cast<unsigned long>(boot_record.pcs[i]));
    if(n<0||static_cast<size_t>(n)>=capacity-used){out[0]=0;return false;}used+=static_cast<size_t>(n);
  }
  n=std::snprintf(out+used,capacity-used,"]}");
  if(n<0||static_cast<size_t>(n)>=capacity-used){out[0]=0;return false;}
  return true;
}
#endif
