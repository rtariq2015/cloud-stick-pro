#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace pro {
enum class DiagnosticCommand { None, Status, Log, Check, Test, Setup, Invalid };
// Discard an entire oversized line, including valid-looking suffixes.
class DiagnosticLine {
  char data[16]{}; unsigned size=0; bool overflow=false;
 public:
  DiagnosticCommand push(char c) {
    if(c=='\r')return DiagnosticCommand::None;
    if(c!='\n') { if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>=127)overflow=true; if(size<sizeof(data)-1&&!overflow)data[size++]=c;else overflow=true; return DiagnosticCommand::None; }
    data[size]=0;
    auto result=DiagnosticCommand::Invalid;
    if(!overflow) {
      if(!size)result=DiagnosticCommand::None;
      else if(!strcmp(data,"STATUS"))result=DiagnosticCommand::Status;
      else if(!strcmp(data,"LOG"))result=DiagnosticCommand::Log;
      else if(!strcmp(data,"CHECK"))result=DiagnosticCommand::Check;
      else if(!strcmp(data,"TEST"))result=DiagnosticCommand::Test;
      else if(!strcmp(data,"SETUP"))result=DiagnosticCommand::Setup;
    }
    size=0;overflow=false;return result;
  }
};
enum class CheckState:uint8_t { Idle, WaitingWifi, WaitingClock, Checking, Ready, Verified, Failed };
inline const char* checkName(CheckState s) {
  switch(s){case CheckState::Idle:return "not_checked";case CheckState::WaitingWifi:return "waiting_wifi";
  case CheckState::WaitingClock:return "waiting_clock";case CheckState::Checking:return "checking";
  case CheckState::Ready:return "ready";case CheckState::Verified:return "test_verified";default:return "failed";}
}
inline const char* errorHelp(int code) {
  switch(code) {
    case 0:return "No error reported";
    case 401:return "Koofr rejected the login. Use your email and a Koofr app password.";
    case 403:return "Cloud account does not allow access to this folder.";
    case 404:return "Cloud backup folder was not found. Check the folder address.";
    case 409:return "Cloud parent folder is missing. Check the folder address.";
    case 412:return "Cloud destination changed or already exists. It was left unchanged; backup will retry.";
    case 429:return "Cloud service is busy. Wait before trying again.";
    case 507:return "Cloud storage is full. Free space in your account.";
    case -2301:return "Save your Wi-Fi and cloud settings first.";
    case -2302:return "Wi-Fi is not connected. Check the selected network or its welcome page.";
    case -2303:return "Waiting for network time. Complete any guest welcome page; check Internet access.";
    case -2002:return "Uploaded data did not match the downloaded verification. Original retained.";
    case -2010:return "USB copying resumed. Backup will retry after copying stops.";
    case -2012:return "Transfer did not finish in time. Original retained; backup will retry.";
    case -2011:return "Cloud destination is a folder or cannot be safely replaced. It was left unchanged.";
    case -2101:return "A folder is too deeply nested. Use fewer than eight folder levels.";
    case -2102:return "The backup folder cannot be read. Create it on CLOUDSTICK and check the SD card.";
    case -2103:return "This scan reached its directory-entry limit. Some files have not been checked.";
    case -2104:return "A filename or path is unsupported. Shorten or rename it on the computer.";
    case -2105:return "Cannot read a file's details from the SD card. Original retained.";
    case -2106:return "The 128-file scan limit was reached. Some files have not been checked.";
    case -2107:return "A file exceeds your configured size limit. Increase the limit in backup settings.";
    case -2108:case -2111:case -2112:return "Internal backup journal is unavailable. Original files are retained.";
    case -2109:case -2003:return "Cannot read the SD card. Original files are retained.";
    case -2110:return "A damaged backup record was requeued. Original files are retained.";
    case -2113:return "Not enough memory to start backups. Restart the dongle.";
    default:return code<0?"Network or HTTPS connection failed. Check Internet access and the trusted certificate.":"Cloud request failed. See the diagnostic error code.";
  }
}
struct DiagnosticEvent {uint32_t boot=0,ms=0;uint64_t time=0;uint8_t kind=0;int32_t code=0;};
class DiagnosticRing {
  std::array<DiagnosticEvent,16> entries{};unsigned next=0,count_=0;
 public:
  void add(DiagnosticEvent e){entries[next]=e;next=(next+1)%entries.size();if(count_<entries.size())++count_;}
  unsigned size()const{return count_;}
  DiagnosticEvent at(unsigned i)const{return i<count_?entries[(next+entries.size()-count_+i)%entries.size()]:DiagnosticEvent{};}
};
// A software restart may retain ownership only if the preceding boot safely
// released the USB disk. Cold, brownout and watchdog boots always give it to USB.
inline bool resumeBackupBoot(bool softwareReset,uint32_t cookie){return softwareReset&&cookie==0x43535042;}
}
