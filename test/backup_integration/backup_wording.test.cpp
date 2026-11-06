#include "pro_display.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <iostream>
#include <string>
#define PROGMEM
#include <Fonts/Font16.c>

static unsigned titleWidth(const char* text){
  unsigned pixels=0;for(size_t i=0;text[i];++i){const auto c=static_cast<unsigned char>(text[i]);assert(c>=32&&c<=126);pixels+=widtbl_f16[c-32];}return pixels;
}
static void fits(const pro::DisplayFrame& f){
  assert(std::strlen(f.title)<24&&std::strlen(f.detail)<=24&&std::strlen(f.footer)<=24);
  assert(titleWidth(f.title)<=148); // lcd_pro_update draws Font2 at x=6.
  for(const char* text:{f.detail,f.footer})for(size_t i=0;text[i];++i)assert(static_cast<unsigned char>(text[i])>=32&&static_cast<unsigned char>(text[i])<=126);
}
static pro::DisplayFrame idle(int error){pro::DisplayInput s;s.wifi=true;s.error=error;s.verified=12;return pro::displayFrame(s);}
int main(){
  struct Fixture{int code;const char* title;const char* help;};
  const Fixture cases[]={
    {-2303,"WAITING FOR TIME","Secure connection waits"},
    {-2302,"WI-FI WAIT","Retry saved Wi-Fi"},
    {-2301,"BACKUP PAUSED","Backup service not ready"},
    {401,"KOOFR LOGIN","Check app password"},
    {403,"CLOUD ACCESS","Check folder permission"},
    {507,"CLOUD FULL","Free cloud storage"},
    {-2001,"CONNECTION WAIT","Check Wi-Fi and clock"},
    {-2002,"VERIFY RETRY","Copy hash did not match"},
    {-2003,"CHECK SD CARD","Read failed; files kept"},
    {-2105,"CHECK SD CARD","Read failed; files kept"},
    {-2109,"CHECK SD CARD","Read failed; files kept"},
    {-2101,"CHECK FILES","Folders nested too deep"},
    {-2102,"CHECK FILES","Backup folder not found"},
    {-2103,"CHECK FILES","Too many folder entries"},
    {-2104,"CHECK FILES","Unsupported file name"},
    {-2106,"CHECK FILES","128-file scan limit"},
    {-2107,"CHECK FILES","File over size limit"},
    {-2108,"BACKUP RECORDS","Queue unavailable/full"},
    {-2111,"BACKUP RECORDS","Queue unavailable/full"},
    {-2112,"BACKUP RECORDS","Queue unavailable/full"},
    {-2110,"RECHECK NEEDED","Backup record repaired"},
    {-2113,"BACKUP PAUSED","Not enough working RAM"},
    {-2115,"SETUP BLOCKED","Queue did not close"},
    {-2010,"PAUSED","Copy/setup; retry later"},
    {-2011,"CLOUD RESPONSE","Check backup folder"},
    {-2012,"TRANSFER WAIT","Transfer did not finish"},
  };
  for(const auto& fixture:cases){
    const auto f=idle(fixture.code);fits(f);
    assert(std::string(f.title)==fixture.title&&std::string(f.detail)==fixture.help);
    assert(f.percent==-1&&std::string(f.footer)=="12 verified");
  }
  std::cout<<"PASS 26 known backup errors have bounded actionable LCD wording\n";
  for(const int error:{-2303,401,403,507,-2107,-2115,-2010,-9999}){
    pro::DisplayInput s;s.wifi=true;s.active=true;s.phase=pro::TransferPhase::Uploading;s.error=error;
    s.failed=2;s.verified=12;s.total=8388608;s.done=4194304;s.percent=50;s.speed=65536;
    const auto f=pro::displayFrame(s);fits(f);assert(std::string(f.title)=="UPLOADING"&&f.percent==50);
  }
  std::cout<<"PASS active transfer takes priority over stale error/failure codes\n";
  pro::DisplayInput s;s.wifi=true;s.verified=12;
  auto f=pro::displayFrame(s);fits(f);assert(std::string(f.title)=="READY"&&f.percent==-1);
  s.hasScan=true;f=pro::displayFrame(s);assert(std::string(f.title)=="READY");
  s.complete=true;s.pending=2;f=pro::displayFrame(s);fits(f);assert(std::string(f.title)=="RETRY QUEUED"&&f.percent==-1);
  s.pending=0;s.error=-2303;f=pro::displayFrame(s);fits(f);assert(std::string(f.title)=="WAITING FOR TIME"&&f.percent==-1);
  s.error=0;s.failed=2;f=pro::displayFrame(s);fits(f);assert(std::string(f.title)=="RETRYING"&&std::string(f.detail)=="Unverified files remain"&&f.percent==-1);
  s.failed=0;f=pro::displayFrame(s);fits(f);assert(std::string(f.title)=="BACKED UP"&&f.percent==100);
  std::cout<<"PASS counts alone and partially verified scans cannot claim completion\n";
  for(const int error:{-9999,412,INT_MIN,INT_MAX}){
    const auto unknown=idle(error);fits(unknown);assert(std::string(unknown.title)=="RETRYING");
    assert(std::strstr(unknown.detail,std::to_string(error).c_str())!=nullptr&&unknown.percent==-1);
  }
  std::cout<<"PASS unknown error retains numeric code and retry action\n";
  uint32_t random=0x71ef4c32;
  for(unsigned i=0;i<10000;++i){
    random=random*1664525u+1013904223u;
    pro::DisplayInput input;input.wifi=(random&1)!=0;input.active=(random&2)!=0;input.complete=(random&4)!=0;
    input.hasScan=(random&8)!=0;input.phase=pro::TransferPhase(random%5);input.error=static_cast<int>(random);
    input.verified=random;input.pending=random>>3;input.failed=random>>6;input.total=UINT64_MAX;input.done=random;
    input.percent=int(random%101);input.speed=random;fits(pro::displayFrame(input));
  }
  std::cout<<"PASS 10000 display inputs retain ASCII/24-character/pinned-font bounds\n";
}
