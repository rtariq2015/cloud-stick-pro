#include "remote_health_model.h"
#if CSP_ENABLE_REMOTE_HEALTH
#include <cstdarg>
#include <cstdio>

namespace pro::health {
namespace {
const char* name(Wifi value){switch(value){case Wifi::Unknown:return "unknown";case Wifi::Disconnected:return "disconnected";case Wifi::Connecting:return "connecting";case Wifi::Connected:return "connected";}return nullptr;}
const char* name(CloudCheck value){switch(value){case CloudCheck::Unknown:return "unknown";case CloudCheck::WaitingWifi:return "waiting-wifi";case CloudCheck::WaitingClock:return "waiting-clock";case CloudCheck::Checking:return "checking";case CloudCheck::ReadAccessOk:return "read-access-ok";case CloudCheck::UploadReadbackOk:return "upload-readback-ok";case CloudCheck::Failed:return "failed";}return nullptr;}
const char* name(UsbOwnership value){switch(value){case UsbOwnership::Unknown:return "unknown";case UsbOwnership::Host:return "host";case UsbOwnership::Ejected:return "ejected";case UsbOwnership::Local:return "local";case UsbOwnership::Fault:return "fault";}return nullptr;}
const char* name(Sd value){switch(value){case Sd::Unknown:return "unknown";case Sd::NotInitialized:return "not-initialized(absent-or-error)";case Sd::Initialized:return "initialized";}return nullptr;}
const char* name(Scan value){switch(value){case Scan::Unknown:return "unknown";case Scan::Partial:return "partial";case Scan::Complete:return "complete";}return nullptr;}
const char* name(Subsystem value){switch(value){case Subsystem::None:return "none";case Subsystem::Wifi:return "wifi";case Subsystem::Clock:return "clock";case Subsystem::Cloud:return "cloud";case Subsystem::Usb:return "usb";case Subsystem::Sd:return "sd";case Subsystem::Queue:return "queue";case Subsystem::Backup:return "backup";case Subsystem::Setup:return "setup";case Subsystem::Health:return "health";}return nullptr;}
bool validTime(uint64_t value){return value==0||(value>=1767225600ULL&&value<=253402300799ULL);}
bool validFirmware(const char (&text)[40]){
  for(size_t i=0;i<sizeof text;++i){
    const char c=text[i];if(!c)return i!=0;
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
  }
  return false;
}
bool valid(const Snapshot& s){
  if(!validFirmware(s.firmware)||!s.boot_id||!s.sequence||!name(s.wifi)||!name(s.cloud_check)||
     !name(s.usb)||!name(s.sd)||!name(s.scan)||!name(s.error_subsystem))return false;
  if(!validTime(s.reported_at)||!validTime(s.wifi_observed_at)||!validTime(s.cloud_checked_at)||
     !validTime(s.capacity_observed_at)||!validTime(s.scan_observed_at)||!validTime(s.last_file_verified_at)||
     !validTime(s.error_observed_at)||s.utc_valid!=(s.reported_at!=0))return false;
  if(s.capacity_known&&(!s.capacity_total||s.capacity_free>s.capacity_total))return false;
  if(s.error_subsystem==Subsystem::None&&(s.error_code||s.error_observed_at))return false;
  return true;
}
class Writer {
 public:
  Writer(char* bytes,size_t capacity):bytes_(bytes),capacity_(capacity<ReportCapacity?capacity:ReportCapacity){}
  void add(const char* format,...){
    if(!ok_)return;
    va_list args;va_start(args,format);
    const int count=std::vsnprintf(bytes_+used_,capacity_-used_,format,args);va_end(args);
    if(count<0||size_t(count)>=capacity_-used_){ok_=false;return;}
    used_+=size_t(count);
  }
  bool ok()const{return ok_;}
  size_t size()const{return used_;}
 private:
  char* bytes_;size_t capacity_,used_=0;bool ok_=true;
};
unsigned long long number(uint64_t value){return static_cast<unsigned long long>(value);}
}

bool encode(const Snapshot& s,char* output,size_t capacity,size_t& length){
  length=0;if(!output||!capacity)return false;output[0]=0;
  if(!valid(s))return false;
  Writer w(output,capacity);
  w.add("Cloud Stick Pro health v1 (private)\nLast reported snapshot; silence means unknown, not power failure.\n");
  w.add("Firmware:%s Boot:%016llx Seq:%llu\n",s.firmware,number(s.boot_id),number(s.sequence));
  w.add("Reported UTC:%llu Clock valid:%u Uptime ms(wraps):%lu\n",number(s.reported_at),unsigned(s.utc_valid),static_cast<unsigned long>(s.uptime_ms));
  w.add("WiFi:%s observed UTC:%llu\n",name(s.wifi),number(s.wifi_observed_at));
  w.add("Cloud check:%s code:%ld checked UTC:%llu\n",name(s.cloud_check),static_cast<long>(s.cloud_code),number(s.cloud_checked_at));
  w.add("USB ownership:%s SD:%s Read view active:%u\n",name(s.usb),name(s.sd),unsigned(s.read_view_active));
  if(s.capacity_known)w.add("Cached capacity bytes total:%llu free:%llu observed UTC:%llu\n",number(s.capacity_total),number(s.capacity_free),number(s.capacity_observed_at));
  else w.add("Cached capacity:unknown\n");
  if(s.scan==Scan::Unknown)w.add("Last scan:unknown; counts unavailable\n");
  else w.add("Last scan:%s observed UTC:%llu pending:%lu failed:%lu verified:%lu skipped:%lu\n",name(s.scan),number(s.scan_observed_at),static_cast<unsigned long>(s.pending),static_cast<unsigned long>(s.failed),static_cast<unsigned long>(s.verified),static_cast<unsigned long>(s.skipped));
  w.add("Last file verified UTC:%llu\n",number(s.last_file_verified_at));
  w.add("Error:%s code:%ld observed UTC:%llu\n",name(s.error_subsystem),static_cast<long>(s.error_code),number(s.error_observed_at));
  w.add("UTC 0=unknown. Counts describe the last scan; partial counts are incomplete.\nEND health-v1 Seq:%llu\n",number(s.sequence));
  if(!w.ok()){output[0]=0;return false;}length=w.size();return true;
}

bool Schedule::due(uint32_t now)const{return !scheduled_||uint32_t(now-at_)>=delay_;}
void Schedule::acknowledged(uint32_t now){at_=now;delay_=SuccessMs;failures_=0;scheduled_=true;acknowledged_=true;}
void Schedule::failed(uint32_t now){
  if(failures_<255)++failures_;
  uint32_t delay=FirstRetryMs;
  for(uint8_t i=1;i<failures_&&delay<MaxRetryMs;++i)delay=delay>MaxRetryMs/2?MaxRetryMs:delay*2;
  at_=now;delay_=delay;scheduled_=true;
}
void Schedule::cancelled(uint32_t now){
  at_=now;scheduled_=true;
  if(!failures_||delay_<FirstRetryMs)delay_=FirstRetryMs;
}
} // namespace pro::health
#endif
