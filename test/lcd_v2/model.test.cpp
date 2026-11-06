#include "pro_lcd_v2.h"
#include <cassert>
#include <climits>
#include <iostream>
#include <cstring>
using namespace pro;
int main(){
  DisplayInput in;in.wifi=true;in.complete=true;in.hasScan=true;in.verified=9;
  auto f=lcd_v2::make(in,true,1700000000,1700000010);
  assert(!strcmp(f.title,"Up to date")&&!strcmp(f.detail,"Last backup <1 min ago")&&f.percent==100);
  assert(!strcmp(f.counts,"9 verified"));
  assert(f.state==lcd_v2::State::Idle&&sizeof(f)==88);
  auto original=f;assert(lcd_v2::equal(f,original));f.wifi=false;assert(!lcd_v2::equal(f,original));
  f=original;f.state=lcd_v2::State::Waiting;assert(!lcd_v2::equal(f,original));
  f=lcd_v2::make(in,true,1700000000,1699999999);assert(!strstr(f.detail,"ago"));
  f=lcd_v2::make(in,true,1700000000,1700000120);assert(!strcmp(f.detail,"Last backup 2 min ago"));
  in.pending=1;f=lcd_v2::make(in,true,0,0);assert(strcmp(f.title,"Up to date")&&f.percent<0);
  in.failed=1;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.counts,"1 need retry"));
  in.pending=in.failed=0;in.active=true;in.phase=TransferPhase::Uploading;in.done=1048576;in.total=2097152;in.percent=50;in.speed=49341;
  f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Uploading")&&!strcmp(f.rate,"49.3 KB/s")&&f.percent==50);
  assert(!strcmp(f.detail,"1.0 / 2.0 MiB"));
  assert(f.busy&&f.state==lcd_v2::State::Active);in.phase=TransferPhase::Reading;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Check USB file"));
  in.phase=TransferPhase::Checking;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Verify cloud"));
  in.phase=TransferPhase::Publishing;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Saving copy")&&!strcmp(f.rate,"Finishing")&&f.percent<0);
  in.phase=TransferPhase::None;in.scanning=true;
  f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Finding QA files")&&!strcmp(f.detail,"Comparing backup records")&&f.percent<0&&!f.rate[0]);
  auto legacy=displayFrame(in);assert(!strcmp(legacy.title,"FINDING FILES")&&!strcmp(legacy.detail,"Comparing backup records"));
  in.scanning=false;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.detail,"Preparing secure backup"));
  // Real completed scans retain their last transfer's phase/counters in RAM.
  // Neither those counters nor a stale scanning flag may become live work.
  in.active=false;in.scanning=true;in.phase=TransferPhase::Checking;in.percent=81;
  f=lcd_v2::make(in,true,1700000000,1700000010);
  assert(!strcmp(f.title,"Up to date")&&f.percent==100&&!f.busy&&!f.rate[0]&&f.state==lcd_v2::State::Idle);
  in.complete=false;f=lcd_v2::make(in,true,0,0);
  assert(!strcmp(f.title,"Waiting to retry")&&!strcmp(f.detail,"Last scan was incomplete")&&f.percent<0&&!f.rate[0]&&f.state==lcd_v2::State::Waiting);
  in.error=-2010;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Backup paused")&&!f.busy&&f.percent<0&&f.state==lcd_v2::State::Attention);
  in.error=0;in.complete=true;in.scanning=false;in.active=true;
  in.phase=TransferPhase::Uploading;in.percent=999;f=lcd_v2::make(in,true,0,0);assert(f.percent==100);
  in.percent=-999;f=lcd_v2::make(in,true,0,0);assert(f.percent==-1);
  char rate[10]{};lcd_v2::speed(rate,0);assert(!strcmp(rate,"0 B/s"));lcd_v2::speed(rate,1500000);assert(!strcmp(rate,"1.5 MB/s"));
  lcd_v2::speed(rate,UINT_MAX);assert(strlen(rate)<sizeof rate);
  in.active=false;in.phase=TransferPhase::None;in.error=401;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Check cloud login")&&f.percent<0);
  in.error=0;in.windowOpen=false;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Night schedule")&&f.state==lcd_v2::State::Waiting);
  in.windowOpen=true;in.quiet=120;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Waiting for idle")&&f.state==lcd_v2::State::Waiting);in.quiet=0;
  in.wifi=false;f=lcd_v2::make(in,true,0,0);assert(!strcmp(f.title,"Wi-Fi offline")&&f.percent<0&&f.state==lcd_v2::State::Offline);
  f=lcd_v2::make(in,false,0,0);assert(!strcmp(f.title,"Check SD card")&&f.percent<0&&!f.busy&&f.state==lcd_v2::State::Attention);
  in.wifi=true;in.complete=false;in.verified=0;in.hasScan=false;f=lcd_v2::make(in,true,0,0);assert(strcmp(f.title,"Up to date"));
  for(unsigned v=0;v<100000;++v){
    in.speed=v*373;in.done=uint64_t(v)*10001;in.total=uint64_t(v)*11001;in.percent=int(v%205)-5;
    in.verified=v%129;in.pending=v%129;in.failed=v%7;in.active=v%2;in.phase=TransferPhase(v%5);
    f=lcd_v2::make(in,v%19!=0,v,v+200);
    assert(f.percent>=-1&&f.percent<=100);
    assert(strlen(f.title)<sizeof f.title&&strlen(f.detail)<sizeof f.detail&&strlen(f.rate)<sizeof f.rate&&strlen(f.counts)<sizeof f.counts);
  }
  char bytes[25]{};lcd_v2::progress(bytes,UINT64_MAX,UINT64_MAX);assert(!strcmp(bytes,"Large file in progress"));
  std::cout<<"PASS LCD local/cloud phases, inactive stale metrics, partial/paused scans, units and100000 bounded frames; sizeof="<<sizeof(f)<<"\n";
}
