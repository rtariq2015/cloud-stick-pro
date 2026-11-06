// Actual owner loop/serial handler with synthetic worker/USB/radio/NVS only.
#include "runtime_fixture_prefix.h"
void fixture(const char* path,bool wrap){
 std::ifstream source(path,std::ios::binary);std::vector<uint8_t> owner{std::istreambuf_iterator<char>(source),{}};assert(owner.size()==432);
 testNvs["csp-owner/owner"]=owner;const std::string config="{\"schema\":1,\"private_fixture\":\"unchanged\"}";
 testNvs["csp/config"]={config.begin(),config.end()};
 pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
 auto& p=saved.profiles[0];p.id=1;std::strcpy(p.ssid.data(),"Synthetic AP");std::strcpy(p.secret.data(),"synthetic-wifi-password");
 WifiNvsStore nvs;pro::wifi_setup::ProfileStore store(nvs);assert(store.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
 if(wrap)testNow=UINT32_MAX-2000;
 setup();tick();assert(workerStarts==1&&!maintenance&&managed.phase==BackupWorkerPhase::Running);
}
int main(int argc,char** argv){
 assert(argc==3);const std::string mode=argv[2];fixture(argv[1],mode=="wrap");
 const auto before=testNvs;const auto writes=nvsWrites;const auto usb=usbInitializations;
 if(mode=="timeout"){
  observedBackup.active=true;openWindow();assert(managed.phase==BackupWorkerPhase::Retiring&&maintenance);
  tick(45001);drained();observedBackup.active=false;tick(60000);
  assert(bleBegins==0&&radioReleases==0&&workerStarts==1&&maintenance);
  const auto denied=command("BENCH_WINDOW");assert(denied["bench_window"]==false);tick();assert(bleBegins==0&&maintenance);
  std::cout<<"PASS genuine 45-second unknown-worker timeout stays blocked after late Drained; no automatic unblock\n";
 }else{
  const unsigned cycles=mode=="repeat"?3:1;
  for(unsigned cycle=0;cycle<cycles;++cycle){
   const auto start=workerStarts,ble=bleBegins,releases=radioReleases;
   observedBackup.active=true;openWindow();assert(managed.phase==BackupWorkerPhase::Retiring&&maintenance&&!bleRunning);
   for(unsigned i=0;i<10;++i)tick(1000);
   assert(bleBegins==ble&&radioReleases==releases&&workerStarts==start&&maintenance);
   drained(false);observedBackup.active=false;tick();assert(bleBegins==ble&&maintenance); // RAII drain alone is insufficient.
   managed.retired_generation=managed.generation-1;reapReady=true;const auto reaps=reapCalls;tick();assert(reapCalls==reaps&&bleBegins==ble);
   managed.retired_generation=managed.generation;tick();
   if(!bleRunning)std::cerr<<"Clock regression: UART window became blocked before valid late reap; drained="<<unsigned(managed.phase)<<'\n';
   assert(bleRunning&&bleBegins==ble+1&&managed.phase==BackupWorkerPhase::Stopped&&maintenance);
   const auto afterReap=reapCalls;tick();tick();assert(reapCalls==afterReap); // No stale-handle re-query after deletion proof.
   closeBle();assert(!maintenance&&workerStarts==start+1&&managed.phase==BackupWorkerPhase::Running);
  }
  std::cout<<"PASS actual UART advancing-clock "<<mode<<" delayed retirement, park/generation gates, BLE admission, same-boot resume\n";
 }
 assert(testNvs==before&&nvsWrites==writes&&usbInitializations==usb&&usb==1&&usbPolls>0&&cloudUploadTests==0);
}
