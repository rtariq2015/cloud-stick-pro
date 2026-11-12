#pragma once
#include "pro_display.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef CSP_ENABLE_LCD_V2
#define CSP_ENABLE_LCD_V2 0
#endif
#if CSP_ENABLE_LCD_V2 != 0 && CSP_ENABLE_LCD_V2 != 1
#error CSP_ENABLE_LCD_V2 must be zero or one
#endif

namespace pro::lcd_v2 {
// RGB565: navy surfaces, white type and a bright blue progress accent. Amber
// retains a separate meaning for faults; Wi-Fi color never claims internet.
constexpr uint16_t Background=0x08A4,Surface=0x1149,Muted=0xA5B9,Track=0x21CB,Good=0x4CFF,Warning=0xFE8C;
enum class State:uint8_t {Idle,Active,Waiting,Offline,Attention};
struct Frame {
  char title[24]{},detail[25]{},rate[10]{},counts[17]{};
  uint16_t accent=Good;
  int percent=-1;
  bool wifi=false,media=false,busy=false;
  State state=State::Idle; // Uses the existing padding byte; Frame stays88B.
};
template<size_t N> inline void text(char(&out)[N],const char* value){
  std::snprintf(out,N,"%s",value?value:"");
}
// Font1 has fourteen cells before the existing Wi-Fi/SD indicators. Consume
// at most one 32-byte SSID, replacing unsupported glyphs without splitting UTF-8.
// This is a local-screen label only; it does not expose names through diagnostics.
inline void wifiLabel(char(&out)[15],bool connected,const char* ssid){
  if(!connected){text(out,"Wi-Fi offline");return;}
  if(!ssid||!*ssid){text(out,"CLOUD STICK");return;}
  char printable[33]{};size_t used=0,at=0;
  while(at<32&&ssid[at]){
    const auto ch=static_cast<unsigned char>(ssid[at++]);
    printable[used++]=ch>=32&&ch<=126?char(ch):'?';
    // Strict SSIDs are UTF-8. Collapse continuation bytes into one placeholder
    // for each unsupported codepoint; malformed input remains bounded as well.
    if(ch>=0xc2&&ch<=0xf4)
      while(at<32&&ssid[at]&&(static_cast<unsigned char>(ssid[at])&0xc0)==0x80)++at;
  }
  text(out,printable);
  if(used>=sizeof out)std::memcpy(out+sizeof out-4,"...",4);
}
inline void counter(char* out,size_t capacity,uint32_t number){
  if(number>999)std::snprintf(out,capacity,"999+");
  else std::snprintf(out,capacity,"%lu",static_cast<unsigned long>(number));
}
inline void speed(char(&out)[10],uint32_t bytes){
  const uint32_t unit=bytes>=1000000?1000000:bytes>=1000?1000:1;
  const char* suffix=unit==1000000?"MB/s":unit==1000?"KB/s":"B/s";
  if(bytes/unit>999){text(out,"999+ MB/s");return;}
  // Decimal units; the byte rate is not an upload-performance estimate.
  if(unit>1&&bytes/unit<100)
    std::snprintf(out,sizeof out,"%u.%u %.4s",unsigned((bytes/unit)%100),unsigned(((bytes%unit)*10/unit)%10),suffix);
  else std::snprintf(out,sizeof out,"%u %.4s",unsigned((bytes/unit)%1000),suffix);
}
inline void progress(char(&out)[25],uint64_t done,uint64_t total){
  // Match the binary byte counts shown by the dashboard, without a float or heap allocation.
  if(done/1048576>999||total/1048576>999){text(out,"Large file in progress");return;}
  if(total>=1048576)
    std::snprintf(out,sizeof out,"%u.%u / %u.%u MiB",unsigned((done/1048576)%1000),unsigned((done%1048576)*10/1048576),
      unsigned((total/1048576)%1000),unsigned((total%1048576)*10/1048576));
  else if(total>=1024)std::snprintf(out,sizeof out,"%u / %u KiB",unsigned(done/1024),unsigned(total/1024));
  else std::snprintf(out,sizeof out,"%u / %u B",unsigned(done),unsigned(total));
}
inline Frame make(const DisplayInput& input,bool media,uint64_t lastBackup,uint64_t now){
  const auto old=displayFrame(input);Frame out;
  text(out.title,old.title);text(out.detail,old.detail);out.accent=old.accent==0xFD20?Warning:Good;
  out.wifi=input.wifi;out.media=media;
  out.percent=old.percent<0?-1:std::min(100,std::max(0,old.percent));
  out.busy=input.active;
  char verified[5]{},pending[5]{},failed[5]{};
  counter(verified,sizeof verified,input.verified);counter(pending,sizeof pending,input.pending);counter(failed,sizeof failed,input.failed);
  if(input.failed)std::snprintf(out.counts,sizeof out.counts,"%s need retry",failed);
  else if(input.pending){
    if(input.verified>999||input.pending>999)text(out.counts,"999+ files");
    else std::snprintf(out.counts,sizeof out.counts,"%u OK %u wait",unsigned(input.verified%1000),unsigned(input.pending%1000));
  }
  else std::snprintf(out.counts,sizeof out.counts,"%s verified",verified);
  if(!media){text(out.title,"Check SD card");text(out.detail,"Storage needs attention");out.accent=Warning;out.percent=-1;out.busy=false;out.state=State::Attention;return out;}
  if(!input.wifi){text(out.title,"Wi-Fi offline");text(out.detail,"Files stay on your USB");out.percent=-1;out.busy=false;out.state=State::Offline;return out;}
  if(input.active&&input.phase!=TransferPhase::None){
    out.state=State::Active;
    switch(input.phase){
      case TransferPhase::Reading:text(out.title,"Check USB file");break;
      case TransferPhase::Uploading:text(out.title,"Uploading");break;
      case TransferPhase::Checking:text(out.title,"Verify cloud");break;
      case TransferPhase::Publishing:text(out.title,"Saving copy");break;
      default:text(out.title,"Working");break;
    }
    progress(out.detail,input.done,input.total);
    if(input.phase==TransferPhase::Publishing){text(out.detail,"Publishing checked copy");text(out.rate,"Finishing");}
    else if(input.speed)speed(out.rate,input.speed);else text(out.rate,"Starting");
    return out;
  }
  if(input.error||input.failed){
    if(input.error==-2303)text(out.title,"Waiting for time");
    else if(input.error==401)text(out.title,"Check cloud login");
    else if(input.error==507)text(out.title,"Cloud is full");
    else if(input.error==-2113||input.error==-2010)text(out.title,"Backup paused");
    else text(out.title,input.failed?"Retry scheduled":"Needs attention");
    out.busy=false;out.state=State::Attention;return out;
  }
  if(input.active){
    out.state=State::Active;
    text(out.title,input.scanning?"Finding QA files":"Connecting");
    text(out.detail,input.scanning?"Comparing backup records":"Preparing secure backup");
    return out;
  }
  if(!input.windowOpen){text(out.title,"Night schedule");out.state=State::Waiting;return out;}
  if(input.quiet){text(out.title,"Waiting for idle");out.state=State::Waiting;return out;}
  if(input.pending){text(out.title,"Retry scheduled");out.state=State::Waiting;return out;}
  if(input.complete&&input.verified){
    text(out.title,"Up to date");
    if(lastBackup&&now>=lastBackup){
      const uint64_t age=now-lastBackup;
      if(age<60)text(out.detail,"Last backup <1 min ago");
      else if(age<3600)std::snprintf(out.detail,sizeof out.detail,"Last backup %llu min ago",static_cast<unsigned long long>(age/60));
      else if(age<86400)std::snprintf(out.detail,sizeof out.detail,"Last backup %llu hr ago",static_cast<unsigned long long>(age/3600));
      else text(out.detail,"Previous backup verified");
    }
    return out;
  }
  if(input.hasScan&&!input.complete){text(out.title,"Waiting to retry");text(out.detail,"Last scan was incomplete");out.state=State::Waiting;return out;}
  text(out.title,"Ready");return out;
}
inline bool equal(const Frame& a,const Frame& b){
  return !std::strcmp(a.title,b.title)&&!std::strcmp(a.detail,b.detail)&&!std::strcmp(a.rate,b.rate)&&!std::strcmp(a.counts,b.counts)&&
    a.accent==b.accent&&a.percent==b.percent&&a.wifi==b.wifi&&a.media==b.media&&a.busy==b.busy&&a.state==b.state;
}
static_assert(sizeof(Frame)==88,"LCD frame must retain its88-byte allocation-free bound");
} // namespace pro::lcd_v2
