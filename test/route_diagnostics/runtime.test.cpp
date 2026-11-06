#include "public_runtime_prefix.h"
#if CSP_PRODUCT_LAB6_PROFILE
namespace pro::owned_guest {
Diagnostic diagnostic(){return {1,3,1};}
}
#endif
int main(int argc,char** argv){
  assert(argc==2);fixture(argv[1]);
  const auto stored=testNvs;const auto writes=nvsWrites,starts=workerStarts,retired=retireRequests,configs=configReads;
  const auto begins=bleBegins,connections=radioConnections,releases=radioReleases;
  for(int code:{-2147483647-1,-1,0,2147483647}){
    routeState.error=pro::owned_guest::Error::ApProof;routeState.native_error=code;
    routeState.generation=0xffffffffu;routeState.station_preserved=true;routeState.resources_retained=false;
    const auto reply=command("PUBLIC_ROUTE_STATUS");
#if CSP_PRODUCT_LAB6_PROFILE
    assert(reply.size()==9&&reply["route_error"]==10&&reply["native_error"].as<int>()==code);
    assert(reply["generation"].as<uint32_t>()==0xffffffffu&&reply["station_preserved"]==true&&reply["resources_retained"]==false);
    assert(reply["failure_phase"]==1&&reply["failure_step"]==3&&reply["ap_proof"]==1);
#else
    assert(reply.size()==1&&reply["accepted"]==false);
#endif
  }
  assert(testNvs==stored&&nvsWrites==writes&&workerStarts==starts&&retireRequests==retired&&configReads==configs);
  assert(bleBegins==begins&&radioConnections==connections&&radioReleases==releases&&usbInitializations==1);
  std::cout<<"PASS actual read-only numeric UART dispatch and disabled admission\n";
}
