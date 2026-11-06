#pragma once
#include "pro_transfer.h"
#include <cstdio>
namespace pro {
struct DisplayInput {
  bool wifi=false,active=false,scanning=false,complete=false,hasScan=false,windowOpen=true;
  uint32_t quiet=0,verified=0,pending=0,failed=0,speed=0;
  uint64_t done=0,total=0;int error=0,percent=-1;
  TransferPhase phase=TransferPhase::None;
};
struct DisplayFrame {char title[24]{},detail[64]{},footer[64]{};uint16_t accent=0x05DF;int percent=-1;bool wifi=false;};
inline DisplayFrame displayFrame(const DisplayInput& s){
  DisplayFrame f;f.wifi=s.wifi;const auto done=(unsigned long long)s.done,total=(unsigned long long)s.total;
  auto text=[&](const char* title,const char* detail,uint16_t color){snprintf(f.title,sizeof f.title,"%s",title);snprintf(f.detail,sizeof f.detail,"%s",detail);f.accent=color;};
  snprintf(f.footer,sizeof f.footer,"%lu verified",(unsigned long)s.verified);
  if(!s.wifi){text("WI-FI OFF","Reconnecting...",0xFD20);f.detail[24]=0;f.footer[24]=0;return f;}
  if(s.active&&s.phase!=TransferPhase::None){
    text(s.phase==TransferPhase::Reading?"READING":s.phase==TransferPhase::Uploading?"UPLOADING":s.phase==TransferPhase::Checking?"CHECKING":"FINISHING","",s.phase==TransferPhase::Checking?0xAD5F:0x05DF);
    snprintf(f.detail,sizeof f.detail,"%llu.%llu / %llu.%llu MB",done/1048576,(done%1048576)*10/1048576,total/1048576,(total%1048576)*10/1048576);
    if(s.total<1048576)snprintf(f.detail,sizeof f.detail,"%llu / %llu KB",done/1024,total/1024);
    if(s.total<1024)snprintf(f.detail,sizeof f.detail,"%llu / %llu bytes",done,total);
    snprintf(f.footer,sizeof f.footer,"%lu KB/s  |  %lu saved",(unsigned long)(s.speed/1024),(unsigned long)s.verified);
    f.percent=s.phase==TransferPhase::Publishing?-1:s.percent;
    if(s.phase==TransferPhase::Publishing)snprintf(f.detail,sizeof f.detail,"Saving verified copy");
    f.detail[24]=0;f.footer[24]=0;return f;
  }
  if(s.error){
    const char* title="RETRYING";const char* detail=nullptr;
    switch(s.error){
      case -2303:title="WAITING FOR TIME";detail="Secure connection waits";break;
      case -2302:title="WI-FI WAIT";detail="Retry saved Wi-Fi";break;
      case -2301:title="BACKUP PAUSED";detail="Backup service not ready";break;
      case 401:title="KOOFR LOGIN";detail="Check app password";break;
      case 403:title="CLOUD ACCESS";detail="Check folder permission";break;
      case 507:title="CLOUD FULL";detail="Free cloud storage";break;
      case -2001:title="CONNECTION WAIT";detail="Check Wi-Fi and clock";break;
      case -2002:title="VERIFY RETRY";detail="Copy hash did not match";break;
      case -2003:case -2105:case -2109:title="CHECK SD CARD";detail="Read failed; files kept";break;
      case -2101:title="CHECK FILES";detail="Folders nested too deep";break;
      case -2102:title="CHECK FILES";detail="Backup folder not found";break;
      case -2103:title="CHECK FILES";detail="Too many folder entries";break;
      case -2104:title="CHECK FILES";detail="Unsupported file name";break;
      case -2106:title="CHECK FILES";detail="128-file scan limit";break;
      case -2107:title="CHECK FILES";detail="File over size limit";break;
      case -2108:case -2111:case -2112:title="BACKUP RECORDS";detail="Queue unavailable/full";break;
      case -2110:title="RECHECK NEEDED";detail="Backup record repaired";break;
      case -2113:title="BACKUP PAUSED";detail="Not enough working RAM";break;
      case -2115:title="SETUP BLOCKED";detail="Queue did not close";break;
      case -2010:title="PAUSED";detail="Copy/setup; retry later";break;
      case -2011:title="CLOUD RESPONSE";detail="Check backup folder";break;
      case -2012:title="TRANSFER WAIT";detail="Transfer did not finish";break;
      default:break;
    }
    text(title,detail?detail:"",0xFD20);
    if(!detail)snprintf(f.detail,sizeof f.detail,"Code %d; retry",s.error);
    f.detail[24]=0;f.footer[24]=0;return f;
  }
  if(s.failed){text("RETRYING","Unverified files remain",0xFD20);f.detail[24]=0;f.footer[24]=0;return f;}
  if(s.active){text(s.scanning?"FINDING FILES":"CONNECTING",s.scanning?"Comparing backup records":"Preparing secure backup",0x05DF);f.detail[24]=0;f.footer[24]=0;return f;}
  if(!s.windowOpen){text("SCHEDULED","Waiting for night window",0xAD5F);f.detail[24]=0;f.footer[24]=0;return f;}
  if(s.quiet){text("WAITING","",0x05DF);snprintf(f.detail,sizeof f.detail,"Backup in %lus if idle",(unsigned long)s.quiet);f.detail[24]=0;f.footer[24]=0;return f;}
  if(s.pending){text("RETRY QUEUED","Files need verification",0xFD20);f.detail[24]=0;f.footer[24]=0;return f;}
  if(s.complete&&s.verified){text("BACKED UP","Your files are verified",0x47D0);f.percent=100;f.detail[24]=0;f.footer[24]=0;return f;}
  text("READY",s.hasScan?"Watching for new files":"Preparing first backup",0x05DF);f.detail[24]=0;f.footer[24]=0;return f;
}
}
