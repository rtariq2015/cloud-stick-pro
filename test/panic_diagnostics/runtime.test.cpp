#include "runtime_fixture_prefix.h"
#include "panic_diagnostics.h"
unsigned panicBegins=0,panicFormats=0;
#if CSP_PANIC_DIAGNOSTICS
void panic_diagnostics_begin(){++panicBegins;}
bool panic_diagnostics_json(char* out,size_t capacity){
  ++panicFormats;
  return std::snprintf(out,capacity,"{\"panic_valid\":false,\"reset_reason\":4,\"core\":0,\"pc\":0,\"count\":0,\"backtrace_corrupt\":false,\"backtrace_truncated\":false,\"pcs\":[]}")<int(capacity);
}
#endif
int main(){
  setup();assert(usbInitializations==1);
  const auto stored=testNvs;const auto writes=nvsWrites,starts=workerStarts,retired=retireRequests,configs=configReads;
  const auto begins=bleBegins,connections=radioConnections,releases=radioReleases;
  const auto reply=command("PANIC_STATUS");
#if CSP_PANIC_DIAGNOSTICS
  assert(panicBegins==1&&panicFormats==1&&reply["panic_valid"].is<bool>()&&!reply["panic_valid"].as<bool>());
  assert(reply["pcs"].is<JsonArrayConst>()&&reply["pcs"].size()==0);
#else
  assert(panicBegins==0&&panicFormats==0&&reply["accepted"]==false);
#endif
  assert(testNvs==stored&&nvsWrites==writes&&workerStarts==starts&&retireRequests==retired&&configReads==configs);
  assert(bleBegins==begins&&radioConnections==connections&&radioReleases==releases&&usbInitializations==1);
  assert(maintenance&&managed.phase==BackupWorkerPhase::Stopped);
  std::cout<<"PASS actual UART feature gate and read-only dispatch\n";
}
