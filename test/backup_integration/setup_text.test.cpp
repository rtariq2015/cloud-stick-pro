#include "pro_setup_text.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#define PROGMEM
#include <Fonts/Font16.c>

static unsigned width(const char* text){
  unsigned pixels=0;
  for(size_t i=0;text[i];++i){
    const auto c=static_cast<unsigned char>(text[i]);assert(c>=32&&c<=126);
    pixels+=widtbl_f16[c-32];
  }
  return pixels;
}
static void check(const char* text){
  const auto frame=pro::setupText(text,width);
  assert(frame.count<=pro::SetupText::Rows);
  static_assert(pro::SetupText::X+pro::SetupText::Width<=160);
  static_assert(pro::SetupText::Y+(pro::SetupText::Rows-1)*pro::SetupText::LineHeight+
                pro::SetupText::GlyphHeight<=80);
  for(unsigned i=0;i<frame.count;++i){
    assert(std::strlen(frame.lines[i].data())<=pro::SetupText::Characters);
    assert(width(frame.lines[i].data())<=pro::SetupText::Width);
  }
}
int main(){
  const char* fixtures[]={"Wi-Fi setup: hold BOOT 3s","Owner card required\nSD out; hold 10s",
      "Ready for owner key\nKeep BOOT held","Pausing safely\nUSB stays available",
      "Finishing setup\nUSB stays available","Wi-Fi connected\nInternet unverified",
      "Backup pause timed out\nFiles stay on USB","Internal queue could not close; setup blocked",
      "Owner authenticated\nChoose Wi-Fi","\r\n\n","",nullptr};
  for(const char* text:fixtures)check(text);
  const auto lines=pro::setupText("Wi-Fi setup\nHold BOOT 3s",width);
  assert(lines.count==2&&!lines.truncated);
  assert(!std::strcmp(lines.lines[0].data(),"Wi-Fi setup"));
  assert(!std::strcmp(lines.lines[1].data(),"Hold BOOT 3s"));
  const auto longWord=pro::setupText(std::string(256,'W').c_str(),width);
  assert(longWord.truncated&&longWord.count==3);
  assert(std::strstr(longWord.lines[2].data(),"...")!=nullptr);
  uint32_t seed=0x4d334c43;
  for(unsigned iteration=0;iteration<10000;++iteration){
    char input[257]{};
    for(unsigned j=0;j<256;++j){seed=seed*1664525u+1013904223u;input[j]=char(seed>>24);}
    check(input);
  }
  std::cout<<"Setup text fits160x80 with pinned TFT Font2;10000 bounded inputs; no physical LCD claim\n";
}
