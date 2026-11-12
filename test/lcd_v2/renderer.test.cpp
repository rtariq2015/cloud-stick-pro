// Actual lcd.cpp against fixed-buffer TFT/Arduino adapters; no device/network.
#include "TFT_eSPI.h"
#include "lcd.h"
#include <atomic>
#include <climits>
#include <new>
#include <iostream>
#include <ctime>
#include "../../src/lcd.cpp"

namespace {
bool tracking=false;
size_t allocations=0;
void* allocate(size_t bytes){if(tracking)++allocations;auto* out=std::malloc(bytes);if(!out)throw std::bad_alloc{};return out;}
}
void* operator new(size_t bytes){return allocate(bytes);}
void* operator new[](size_t bytes){return allocate(bytes);}
void operator delete(void* ptr)noexcept{std::free(ptr);}
void operator delete[](void* ptr)noexcept{std::free(ptr);}
void operator delete(void* ptr,size_t)noexcept{std::free(ptr);}
void operator delete[](void* ptr,size_t)noexcept{std::free(ptr);}

bool backup_window_open(){return lcdmock::windowOpen;}
BackupPolicy backup_policy(){return {};}
IPAddress setup_network_ip(){return {};}

namespace {
BackupStatus base(){
  BackupStatus s;s.verified=9;s.scanComplete=true;s.hasScan=true;
  s.lastBackup=uint64_t(std::time(nullptr))-3;
  std::strcpy(s.message,"PRIVATE_PASSWORD_MUST_NOT_APPEAR");
  std::strcpy(s.currentFile,"PRIVATE_FILENAME_MUST_NOT_APPEAR.pdf");return s;
}
void privateCheck(){
  for(size_t i=0;i<lcdmock::used;++i){
    assert(!std::strstr(lcdmock::calls[i].text,"PRIVATE_"));
    assert(!std::strstr(lcdmock::calls[i].text,"owner-key"));
  }
}
void update(const BackupStatus& s,pro::Owner owner=pro::Owner::Host,bool wifi=true,const char* ssid=nullptr){
  lcdmock::clearCalls();const auto before=allocations;tracking=true;
  lcd_pro_update(s,owner,wifi,ssid);tracking=false;
  assert(allocations==before);privateCheck();
}
bool has(const char* text){
  for(size_t i=0;i<lcdmock::used;++i)if(!std::strcmp(lcdmock::calls[i].text,text))return true;
  return false;
}
void rows(){
  for(size_t i=0;i<lcdmock::used;++i){const auto& call=lcdmock::calls[i];
    if(call.kind!='T')continue;
    if(call.y==4)assert(call.h==8&&call.x>=6&&call.x+call.w<=154);
    if(call.y==20)assert(call.h==16&&call.x==6&&call.x+call.w<=116);
    if(call.y==24)assert(call.h==8&&call.x>=120&&call.x+call.w<=154);
    if(call.y==40)assert(call.h==8&&call.x==6&&call.x+call.w<=154);
    if(call.y==66){assert(call.h==8);if(call.x==6)assert(call.x+call.w<=96);else assert(call.x>=100&&call.x+call.w==154);}
  }
}
void ppm(const std::string& directory,const char* name){
  const auto path=directory+"/"+name+".ppm";FILE* file=std::fopen(path.c_str(),"wb");assert(file);
  assert(std::fprintf(file,"P6\n160 80\n255\n")>0);
  for(uint16_t pixel:lcdmock::pixels){
    const unsigned char rgb[]={static_cast<unsigned char>(((pixel>>11)&31)*255/31),
      static_cast<unsigned char>(((pixel>>5)&63)*255/63),static_cast<unsigned char>((pixel&31)*255/31)};
    assert(std::fwrite(rgb,1,3,file)==3);
  }
  assert(std::fclose(file)==0);
}
void capture(const std::string& directory,const char* name,BackupStatus state,
             uint32_t tick=0,pro::Owner owner=pro::Owner::Host,bool wifi=true,const char* ssid="CSP Home"){
  lcdmock::now=tick;proRedraw=true;lcdmock::raster=true;update(state,owner,wifi,ssid);rows();ppm(directory,name);lcdmock::raster=false;
}
void captureText(const std::string& directory,const char* name,const char* message){
  lcdmock::clearCalls();lcdmock::raster=true;const auto before=allocations;tracking=true;
  lcd_show_text(message);tracking=false;assert(allocations==before);privateCheck();
  for(size_t i=0;i<lcdmock::used;++i){const auto& call=lcdmock::calls[i];
    if(call.kind=='T'&&call.font==2)assert(call.x==6&&call.y>=22&&call.y+call.h<=80&&call.w<=148);}
  ppm(directory,name);lcdmock::raster=false;
}
}

int main(int argc,char** argv){
  assert(argc==2);const std::string directory=argv[1];
  lcd_init();assert(lcdmock::rotation==1&&lcdmock::backlightPin==38&&lcdmock::backlightValue==LOW);
  auto state=base();update(state);assert(lcdmock::used>0);
#if !CSP_ENABLE_LCD_V2
  assert(has("BACKED UP"));update(state);assert(lcdmock::used==0);
  std::cout<<"PASS actual renderer default-off keeps legacy pro display\n";return 0;
#else
  assert(has("Up to date")&&has("9 verified")&&has("IDLE"));
  rows();
  update(state);assert(lcdmock::used==0);
  update(state,pro::Owner::Host,true,"Guest-Net");assert(has("Guest-Net")&&has("Up to date"));rows();
  update(state,pro::Owner::Host,true,"Guest-Net");assert(lcdmock::used==0);
  update(state,pro::Owner::Host,true,"CSP Travel");assert(has("CSP Travel")&&!has("Guest-Net"));rows();
  update(state,pro::Owner::Host,false,"CSP Travel");assert(has("Wi-Fi offline")&&!has("CSP Travel"));rows();
  update(state,pro::Owner::Host,true,"12345678901234567890123456789012");assert(has("12345678901..."));rows();
  update(state,pro::Owner::Host,true,"Caf\xc3\xa9");assert(has("Caf?"));rows();
  update(state,pro::Owner::Host,true,"Bad\n");assert(has("Bad?"));rows();
  state.active=true;state.phase=pro::TransferPhase::Uploading;
  update(state,pro::Owner::Host,true,"Guest-Net");assert(has("Guest-Net")&&has("Uploading"));rows();
  state=base();state.error=-4;
  update(state,pro::Owner::Host,true,"Guest-Net");assert(has("Guest-Net")&&has("Needs attention")&&has("Code -4; retry"));rows();
  state=base();update(state);
  lcdmock::now=250;update(state);assert(lcdmock::used==0); // Idle does not animate.
  lcd_clear();update(state);assert(lcdmock::used>0&&has("Up to date"));
  for(const char* message:{"Owner setup\nUSB stays available","Wi-Fi setup: hold BOOT 3s",
      "BLE needs review\nSetup did not close safely",
      "Setup blocked: BLE needs review; backup remains paused"}){
    lcdmock::clearCalls();lcd_show_text(message);
    for(size_t i=0;i<lcdmock::used;++i){const auto& call=lcdmock::calls[i];
      if(call.kind=='T'&&call.font==2)assert(call.x==6&&call.y>=22&&call.y+call.h<=80&&call.w<=148);}
    if(std::strstr(message,"BLE needs review")){
      bool retained=false;for(size_t i=0;i<lcdmock::used;++i)retained|=std::strstr(lcdmock::calls[i].text,"review")!=nullptr;
      assert(retained);
    }
    update(state);assert(has("Up to date"));
  }
  lcd_show_success("Saved");update(state);assert(has("Up to date"));
  lcd_show_error("Error");update(state);assert(has("Up to date"));
  lcd_show_loading();update(state);assert(has("Up to date"));
  lcd_init();update(state);assert(has("Up to date"));
  state.active=true;state.scanning=true;state.phase=pro::TransferPhase::None;state.scanComplete=false;
  update(state);assert(has("Finding QA files")&&has("Comparing backup records")&&has("BUSY")&&!has("Checking cloud access"));rows();
  state.active=false;state.phase=pro::TransferPhase::Checking;state.progress=90;state.transferSpeed=49341;
  update(state);assert(has("Waiting to retry")&&has("Last scan was incomplete")&&has("WAIT"));rows();
  state.scanComplete=true;update(state);assert(has("Up to date")&&has("IDLE")&&!has("49.3 KB/s")&&!has("90%"));
  state=base();
  for(unsigned pane=0;pane<3;++pane){
    lcdmock::now=pane*7000;update(state,pro::Owner::Fault);
    if(lcdmock::used){assert(has("Check SD card")&&has("Storage needs attention")&&has("CHECK"));
      for(size_t i=0;i<lcdmock::used;++i)if(!std::strcmp(lcdmock::calls[i].text,"SD"))assert(lcdmock::calls[i].color==pro::lcd_v2::Warning);}
  }
  state.active=true;state.phase=pro::TransferPhase::Publishing;state.progress=-1;
  update(state);const auto clears=lcdmock::fullClears;update(state);assert(!lcdmock::used);
  lcdmock::now+=250;update(state);assert(lcdmock::used==2&&lcdmock::fullClears==clears);
  for(size_t i=0;i<lcdmock::used;++i)assert(lcdmock::calls[i].y==54);
  // Fault overrides media after model construction. It must never animate or
  // retain the percentage badge of an interrupted transfer.
  update(state,pro::Owner::Fault);assert(has("CHECK"));const auto faultCalls=lcdmock::totalCalls;
  lcdmock::now+=250;update(state,pro::Owner::Fault);assert(lcdmock::totalCalls==faultCalls);
  // Title clipping uses the pinned variable Font2 widths and leaves the badge.
  pro::lcd_v2::Frame worst;std::strcpy(worst.title,"WWWWWWWWWWWWWWWWWWWWWWW");
  std::strcpy(worst.detail,"WWWWWWWWWWWWWWWWWWWWWWWW");std::strcpy(worst.rate,"999+ MB/s");
  std::strcpy(worst.counts,"999+ need retry");worst.percent=100;proRedraw=true;lcdmock::clearCalls();
  lcd_v2_render(worst);rows();bool clipped=false;
  for(size_t i=0;i<lcdmock::used;++i){const auto& call=lcdmock::calls[i];
    if(call.kind=='T'&&call.y==20){assert(call.x+call.w<=116);clipped=std::strstr(call.text,"...");}}
  assert(clipped);
  // Entire actual adapter with bounded extreme/cancel/error/schedule inputs.
  for(unsigned i=0;i<10000;++i){
    state=base();state.active=i%2;state.scanning=i%3==0;state.progress=int(i%703)-301;
    state.phase=pro::TransferPhase(i%5);state.transferSpeed=uint32_t(i)*491057u;
    state.transferDone=uint64_t(i)*100003;state.transferTotal=uint64_t(i)*100007;
    state.verified=i%1001;state.pending=i%17;state.failed=i%9;
    state.error=i%23==0?INT_MIN:i%5==0?401:0;
    lcdmock::media=i%31!=0;lcdmock::quiet=i%7==0?120:0;lcdmock::windowOpen=i%11!=0;
    lcdmock::now=i*251u;update(state,i%29==0?pro::Owner::Fault:pro::Owner::Host,i%13!=0);rows();
  }
  lcdmock::media=true;lcdmock::quiet=0;lcdmock::windowOpen=true;
  state=base();capture(directory,"up-to-date",state);
  capture(directory,"wifi-guest",state,0,pro::Owner::Host,true,"Guest-Net");
  state.error=-4;capture(directory,"wifi-cloud-retry",state,0,pro::Owner::Host,true,"Guest-Net");state.error=0;
  state.lastBackup-=3600;capture(directory,"one-hour",state);
  state=base();capture(directory,"setup-hint",state,14000);capture(directory,"local-status",state,7000);
  state.active=true;state.progress=50;state.transferDone=1048576;state.transferTotal=2097152;state.transferSpeed=49341;
  state.phase=pro::TransferPhase::Reading;capture(directory,"checking-usb",state);
  state.phase=pro::TransferPhase::Uploading;capture(directory,"uploading",state);
  state.phase=pro::TransferPhase::Checking;capture(directory,"verifying",state);
  state.phase=pro::TransferPhase::Publishing;capture(directory,"saving",state);
  state=base();state.active=true;state.scanning=true;state.scanComplete=false;
  capture(directory,"finding-files",state);
  state.scanning=false;capture(directory,"preparing",state);
  state.active=false;state.scanning=true;state.phase=pro::TransferPhase::Checking;state.progress=73;
  state.transferSpeed=49341;state.transferDone=3746;state.transferTotal=3746;
  capture(directory,"partial-scan",state);
  state.scanComplete=true;capture(directory,"inactive-last-transfer",state);
  state.error=-2010;capture(directory,"paused",state);
  state=base();state.pending=2;capture(directory,"retry",state);
  state.failed=1;state.error=507;capture(directory,"cloud-full",state);
  state.error=401;capture(directory,"cloud-login",state);
  state=base();capture(directory,"wifi-offline",state,0,pro::Owner::Host,false);
  lcdmock::media=false;capture(directory,"no-card",state);lcdmock::media=true;
  capture(directory,"card-fault",state,0,pro::Owner::Fault);
  lcdmock::quiet=120;capture(directory,"waiting-idle",state);lcdmock::quiet=0;
  lcdmock::windowOpen=false;capture(directory,"scheduled",state);lcdmock::windowOpen=true;
  captureText(directory,"setup-instructions","Owner setup\nUSB stays available");
  captureText(directory,"setup-review","BLE needs review\nSetup did not close safely");
  captureText(directory,"setup-blocked","Setup blocked: BLE needs review; backup remains paused");
  std::cout<<"PASS actual lcd.cpp: pinned Font1/Font2,160x80 geometry,10000 extreme frames, redraw/animation,0 C++ render allocations,no private canaries; Frame="
           <<sizeof(pro::lcd_v2::Frame)<<" bytes\n";
#endif
}
