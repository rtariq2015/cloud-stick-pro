#include "remote_health_model.h"
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>

static bool tracking=false;static size_t allocations=0;
void* operator new(size_t n){if(tracking)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
using namespace pro::health;
Snapshot sample(){
  Snapshot s;std::strcpy(s.firmware,"0.4.0-product-lab3");s.boot_id=0x1234567890abcdefULL;s.sequence=17;
  s.utc_valid=true;s.reported_at=1793828957;s.uptime_ms=654321;
  s.wifi=Wifi::Connected;s.wifi_observed_at=s.reported_at-2;
  s.cloud_check=CloudCheck::ReadAccessOk;s.cloud_code=207;s.cloud_checked_at=s.reported_at-60;
  s.usb=UsbOwnership::Host;s.sd=Sd::Initialized;
  s.capacity_known=true;s.capacity_total=32000000000ULL;s.capacity_free=123456789;s.capacity_observed_at=s.reported_at-90;
  s.scan=Scan::Partial;s.scan_observed_at=s.reported_at-70;s.pending=3;s.failed=1;s.verified=2;s.skipped=1;
  s.last_file_verified_at=s.reported_at-100;s.error_subsystem=Subsystem::Backup;s.error_code=-2010;s.error_observed_at=s.reported_at-65;
  return s;
}
std::string report(const Snapshot& s){
  char buffer[ReportCapacity];size_t length=0;const auto before=allocations;
  tracking=true;const bool ok=encode(s,buffer,sizeof buffer,length);tracking=false;
  assert(ok&&length>0&&length<ReportCapacity&&length==std::strlen(buffer)&&allocations==before);
  return std::string(buffer,length);
}
void reject(const Snapshot& s){char bytes[ReportCapacity];std::memset(bytes,'X',sizeof bytes);size_t length=123;assert(!encode(s,bytes,sizeof bytes,length));assert(length==0&&bytes[0]==0);}

int main(){
  static_assert(ReportCapacity==1024);static_assert(sizeof(Schedule)<=16);
  auto s=sample();const auto text=report(s);
  assert(text.find("Last reported snapshot; silence means unknown, not power failure.")!=std::string::npos);
  assert(text.find("Cloud check:read-access-ok code:207")!=std::string::npos);
  assert(text.find("Last file verified UTC:1793828857")!=std::string::npos);
  assert(text.find("Last scan:partial")!=std::string::npos&&text.find("partial counts are incomplete")!=std::string::npos);
  assert(text.find("Read view active:0")!=std::string::npos&&text.find("SD:initialized")!=std::string::npos);
  for(const char* forbidden:{"SSID","password","credential","currentFile","username","https://","/sdcard/"})assert(text.find(forbidden)==std::string::npos);
  assert(text.find("END health-v1 Seq:17\n")!=std::string::npos);
  for(size_t capacity=1;capacity<=text.size();++capacity){
    std::vector<char> buffer(capacity+8,'G');size_t length=99;
    assert(!encode(s,buffer.data()+4,capacity,length));assert(length==0&&buffer[4]==0);
    for(size_t i=0;i<4;++i)assert(buffer[i]=='G'&&buffer[capacity+4+i]=='G');
  }
  {std::vector<char> buffer(text.size()+9,'G');size_t length=0;
    assert(encode(s,buffer.data()+4,text.size()+1,length));assert(length==text.size()&&buffer.back()=='G');}
  {char buffer[ReportCapacity+16];std::memset(buffer,'G',sizeof buffer);size_t length=0;
    assert(encode(s,buffer,sizeof buffer,length));for(size_t i=ReportCapacity;i<sizeof buffer;++i)assert(buffer[i]=='G');}
  {size_t length=99;char byte='X';assert(!encode(s,nullptr,1024,length)&&length==0);length=99;
    assert(!encode(s,&byte,0,length)&&length==0&&byte=='X');}
  {Snapshot minimal;std::strcpy(minimal.firmware,"test");minimal.boot_id=1;minimal.sequence=1;
    auto t=report(minimal);assert(t.find("Last scan:unknown; counts unavailable")!=std::string::npos);
    assert(t.find("Cached capacity:unknown")!=std::string::npos);assert(t.find("Reported UTC:0 Clock valid:0")!=std::string::npos);}
  {auto bad=s;std::memset(bad.firmware,'X',sizeof bad.firmware);reject(bad);bad=s;bad.firmware[0]=0;reject(bad);
    for(char c:{'\n','/','<',' ','\t',char(0xff)}){bad=s;bad.firmware[1]=c;reject(bad);}}
  {auto bad=s;bad.boot_id=0;reject(bad);bad=s;bad.sequence=0;reject(bad);
    bad=s;bad.wifi=static_cast<Wifi>(255);reject(bad);bad=s;bad.cloud_check=static_cast<CloudCheck>(255);reject(bad);
    bad=s;bad.usb=static_cast<UsbOwnership>(255);reject(bad);bad=s;bad.sd=static_cast<Sd>(255);reject(bad);
    bad=s;bad.scan=static_cast<Scan>(255);reject(bad);bad=s;bad.error_subsystem=static_cast<Subsystem>(255);reject(bad);}
  {auto bad=s;bad.reported_at=1;reject(bad);bad=s;bad.utc_valid=false;reject(bad);
    bad=s;bad.reported_at=0;reject(bad);bad=s;bad.cloud_checked_at=UINT64_MAX;reject(bad);
    bad=s;bad.capacity_free=bad.capacity_total+1;reject(bad);bad=s;bad.capacity_total=bad.capacity_free=0;reject(bad);
    bad=s;bad.error_subsystem=Subsystem::None;reject(bad);}
  {auto cached=s;cached.capacity_known=false;cached.read_view_active=false;cached.sd=Sd::NotInitialized;
    const auto t=report(cached);assert(t.find("Cached capacity:unknown")!=std::string::npos);
    assert(t.find("not-initialized(absent-or-error)")!=std::string::npos&&t.find("SD failed")==std::string::npos);}
  {auto maximum=s;std::memset(maximum.firmware,'X',39);maximum.firmware[39]=0;
    maximum.boot_id=maximum.sequence=UINT64_MAX;maximum.reported_at=maximum.wifi_observed_at=maximum.cloud_checked_at=
      maximum.capacity_observed_at=maximum.scan_observed_at=maximum.last_file_verified_at=maximum.error_observed_at=253402300799ULL;
    maximum.uptime_ms=UINT32_MAX;maximum.cloud_code=maximum.error_code=INT32_MIN;
    maximum.capacity_total=maximum.capacity_free=UINT64_MAX;maximum.pending=maximum.failed=maximum.verified=maximum.skipped=UINT32_MAX;
    maximum.wifi=Wifi::Disconnected;maximum.usb=UsbOwnership::Unknown;maximum.scan=Scan::Complete;
    maximum.cloud_check=CloudCheck::UploadReadbackOk;maximum.sd=Sd::NotInitialized;maximum.error_subsystem=Subsystem::Health;
    const auto t=report(maximum);std::cout<<"maximum_report_bytes="<<t.size()<<" snapshot_bytes="<<sizeof(Snapshot)<<" schedule_bytes="<<sizeof(Schedule)<<'\n';}
  {Schedule schedule;assert(schedule.due(0)&&!schedule.has_acknowledgment());schedule.cancelled(5);
    assert(!schedule.has_acknowledgment()&&schedule.failures()==0&&!schedule.due(5+Schedule::FirstRetryMs-1));
    assert(schedule.due(5+Schedule::FirstRetryMs));
    schedule.acknowledged(100);assert(schedule.has_acknowledgment()&&schedule.failures()==0);
    assert(!schedule.due(100+Schedule::SuccessMs-1)&&schedule.due(100+Schedule::SuccessMs));
    const uint32_t delays[]={300000,600000,1200000,2400000,3600000,3600000};
    for(auto delay:delays){schedule.failed(1000);assert(schedule.delay_ms()==delay&&!schedule.due(1000+delay-1)&&schedule.due(1000+delay));}
    const auto failures=schedule.failures();schedule.cancelled(2000);assert(schedule.failures()==failures&&schedule.delay_ms()==Schedule::MaxRetryMs);
    for(unsigned i=0;i<300;++i)schedule.failed(3000);assert(schedule.failures()==255&&schedule.delay_ms()==Schedule::MaxRetryMs);
    schedule.acknowledged(4000);assert(schedule.failures()==0&&schedule.delay_ms()==Schedule::SuccessMs);}
  {Schedule schedule;const uint32_t start=UINT32_MAX-1000;schedule.acknowledged(start);
    assert(!schedule.due(start+Schedule::SuccessMs-1)&&schedule.due(start+Schedule::SuccessMs));
    schedule.failed(start);assert(!schedule.due(start+Schedule::FirstRetryMs-1)&&schedule.due(start+Schedule::FirstRetryMs));}
  std::cout<<"PASS bounded encoder, no allocation, truthful unknowns, rejection, retry and wrap tests\n";
  return 0;
}
