// Actual merged managed worker, health observer/model, history cache and event
// log. RTOS, SD and network transports are synthetic; no board/cloud access.
#include "fakes.h"
#include "backup_worker.h"
#include "file_history.h"
#include "pro_event_log.h"
#include "../event_log/io.h"
#include <filesystem>
#include <iostream>
#include <cerrno>
#include <dirent.h>

extern "C" DIR* __wrap_opendir(const char* path) {
  assert(std::string(path)=="/sdcard/SYNTHETIC_COMBINED_MISSING");
  assert(fake::snapshotOpen&&fake::healthTLS==0&&fake::cloudTLS==0);
  errno=ENOENT;return nullptr; // Never ask the host filesystem to enumerate SD.
}

namespace {
template<class P> void until(P ready) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
  while(!ready()) {assert(std::chrono::steady_clock::now()<end);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
CloudConfig config() {
  CloudConfig c;c.is_valid=true;c.watch_folder="SYNTHETIC_COMBINED_MISSING";
  c.server_url="https://synthetic.invalid/";c.username="owner";
  c.password="synthetic";c.ca_pem="synthetic";c.sync_interval_s=1000000;
  return c;
}
size_t eventIndex(const std::string& value) {
  std::lock_guard<std::mutex> lock(fake::mutex);
  const auto at=std::find(fake::events.begin(),fake::events.end(),value);
  assert(at!=fake::events.end());return size_t(at-fake::events.begin());
}
void drainedAndReaped(uint32_t generation) {
  until([&]{const auto s=backup_worker_snapshot();
    return s.phase==BackupWorkerPhase::Drained&&s.retired_generation==generation;});
  assert(backup_worker_snapshot().error==0);
  until([]{return backup_worker_reap();});
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Stopped);
  assert(fake::tasks.empty()&&fake::allocations.empty()&&!fake::metadataMounted);
}
void noWorkerResources() {
  assert(fake::healthTLS==0&&fake::cloudTLS==0&&!fake::snapshotOpen);
  assert(fake::journalLive==0&&fake::remoteLive==0&&fake::engineLive==0);
}
}

void* combined_history_alloc(size_t bytes) {
  assert(bytes==2240&&fake::metadataMounted&&fake::journalLive==1);
  assert(fake::healthTLS==0&&fake::cloudTLS==0&&!fake::snapshotOpen);
  assert(fake::remoteLive==0&&fake::engineLive==0&&maintenance_active());
  assert(event_io::paths.empty()&&!pro::events::status().dirty);
  ++fake::historyAllocs;fake::event("history allocated");return std::malloc(bytes);
}
void combined_history_free(void* ptr) {
  assert(ptr);const auto* bytes=static_cast<const unsigned char*>(ptr);
  for(size_t i=0;i<2240;++i)assert(bytes[i]==0); // Actual cache release erases it.
  ++fake::historyFrees;fake::event("history erased and freed");std::free(ptr);
}

int main(int argc,char** argv) {
  assert(argc==2);const std::string mode=argv[1];
  assert(mode=="normal"||mode=="held-view"||mode=="retire-health"||
         mode=="maintenance-health"||mode=="cloud-tls"||mode=="health-failure");
  std::filesystem::create_directories(CSP_EVENT_LOG_ROOT);
  pro::Record receipt;receipt.path="SYNTHETIC_COMBINED_MISSING/test.pdf";
  receipt.destination=std::string(64,'a');receipt.verifiedAt=1767225601;
  pro::syntheticReceipts.push_back(receipt);
  fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;fake::capacityOk=true;
  fake::holdSnapshot=mode=="held-view";fake::holdCheck=mode=="cloud-tls";
  fake::holdHealth=mode=="retire-health"||mode=="maintenance-health";
  if(mode=="health-failure")fake::healthCode=507;
  fake::onEvent=[](const std::string& value) {
    if(value=="remote created"||value=="engine created"||value=="health TLS opened")
      assert(pro::history::cacheBytesForTest()==0&&!pro::history::status().loaded);
    if(value=="history capture started") {
      assert(fake::healthTLS==0&&fake::cloudTLS==0&&!fake::snapshotOpen);
      assert(fake::remoteLive==0&&fake::engineLive==0&&event_io::paths.empty());
      assert(!pro::events::status().dirty);
    }
  };
  event_io::before=[](const char*) {
    assert(fake::metadataMounted&&!fake::snapshotOpen&&fake::healthTLS==0&&fake::cloudTLS==0);
  };
  assert(backup_worker_start(config()));uint32_t retiring=0;
  if(mode=="held-view") {
    until([]{return fake::snapshotEntered.load();});
    assert(fake::snapshotOpen&&fake::healthCalls==0&&fake::captures==0);
    retiring=backup_worker_request_retire();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring&&fake::captures==0);
    fake::release(fake::holdSnapshot);
  } else if(mode=="cloud-tls") {
    until([]{return fake::checkEntered.load();});
    assert(fake::cloudTLS==1&&fake::healthCalls==0&&fake::captures==0);
    retiring=backup_worker_request_retire();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring&&fake::captures==0);
    fake::release(fake::holdCheck);
  } else {
    until([]{return fake::healthCalls.load()==1;});
    assert(!fake::snapshotOpen&&fake::captures==0&&pro::history::cacheBytesForTest()==0);
    if(mode=="retire-health"||mode=="maintenance-health") {
      assert(fake::healthTLS==1);
      if(mode=="maintenance-health")set_maintenance(true);
      else retiring=backup_worker_request_retire();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      assert(fake::captures==0&&fake::metadataMounted&&fake::healthDone==0);
      assert(backup_worker_snapshot().phase==(retiring?BackupWorkerPhase::Retiring:BackupWorkerPhase::Running));
      fake::release(fake::holdHealth);
      until([]{return fake::healthDone.load()==1&&fake::healthTLS==0;});
      if(!retiring) {
        fake::clock.fetch_add(pro::health::Schedule::SuccessMs+1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(fake::healthCalls==1&&fake::captures==0);
        retiring=backup_worker_request_retire();
      }
    } else {
      until([]{return fake::healthDone.load()==1&&fake::healthTLS==0;});
      if(mode=="health-failure")assert(backup_status().error==-2102);
      retiring=backup_worker_request_retire();
    }
  }
  assert(retiring);drainedAndReaped(retiring);noWorkerResources();
  assert(fake::captures==1&&fake::historyAllocs==1&&fake::historyFrees==0);
  assert(pro::history::cacheBytesForTest()==2240&&pro::history::status().loaded);
  assert(pro::history::status().count==1&&pro::history::status().complete);
  pro::history::Fragment row;
  assert(pro::history::read(pro::history::status().revision,0,0,row)==pro::history::Read::Found);
  assert(row.verified==receipt.verifiedAt&&row.path_bytes==receipt.path.size());
  if(mode=="held-view")assert(eventIndex("snapshot released")<eventIndex("history capture started"));
  if(mode=="cloud-tls")assert(eventIndex("TLS destroyed")<eventIndex("history capture started"));
  if(fake::healthCalls)assert(eventIndex("health TLS destroyed")<eventIndex("history capture started"));
  assert(eventIndex("engine destroyed")<eventIndex("history capture started"));
  assert(eventIndex("remote destroyed")<eventIndex("history capture started"));
  assert(eventIndex("history capture started")<eventIndex("metadata unmounted"));
  assert(eventIndex("metadata unmounted")<eventIndex("task deleted"));
  assert(event_io::paths.empty()&&!pro::events::status().dirty);
  // The runtime's real exclusion/probe paths have separate adapter coverage.
  // Here exercise their contract with the actual retired worker/cache: BLE
  // retains a read-only cache but cannot keep any worker/health socket alive.
  const auto published=fake::healthCalls.load();fake::ble=true;
  fake::clock.fetch_add(pro::health::Schedule::SuccessMs+1);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  assert(fake::healthCalls==published&&pro::history::cacheBytesForTest()==2240);
  noWorkerResources();fake::ble=false;
  // A new admitted worker must erase the actual cache before creating a remote
  // object or checking cloud, including when a later allocation fails.
  fake::holdCheck=true;assert(backup_worker_start(config()));
  assert(pro::history::cacheBytesForTest()==0&&!pro::history::status().loaded);
  until([]{return fake::checkCalls.load()==2;});
  assert(fake::historyFrees==1&&fake::cloudTLS==1);
  const auto second=backup_worker_request_retire();fake::release(fake::holdCheck);
  drainedAndReaped(second);assert(pro::history::cacheBytesForTest()==2240);
  assert(fake::captures==2&&fake::historyAllocs==2);
  pro::history::release();assert(pro::history::cacheBytesForTest()==0&&!pro::history::status().loaded);
  // No portal implementation runs in this worker-only gate. Its required
  // admission is measured here after an explicit real cache erase.
  noWorkerResources();assert(fake::historyFrees==2&&backup_worker_snapshot().phase==BackupWorkerPhase::Stopped);
  fake::event("portal probe contract admitted with zero history cache");
  assert(pro::events::status().loaded&&!pro::events::status().dirty&&event_io::paths.empty());
  std::cout<<"PASS merged worker + real history cache/health observer/events: "<<mode<<'\n';
}
