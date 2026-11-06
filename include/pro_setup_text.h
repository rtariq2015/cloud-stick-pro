#pragma once
#include <array>
#include <cstddef>
#include <cstring>

namespace pro {
// Fixed, allocation-free layout for the 160x80 setup screen. The adapter passes
// TFT_eSPI's real Font2 width measure; tests use that pinned font's width table.
struct SetupText {
  static constexpr unsigned Width=148,Rows=3,Characters=48;
  static constexpr unsigned X=6,Y=22,LineHeight=18,GlyphHeight=16;
  std::array<std::array<char,Characters+1>,Rows> lines{};
  unsigned count=0;
  bool truncated=false;
};
template<class Measure>
SetupText setupText(const char* message,Measure measure){
  SetupText frame;if(!message)return frame;
  constexpr size_t InputLimit=256;size_t at=0;
  while(frame.count<SetupText::Rows&&at<InputLimit&&message[at]){
    while(at<InputLimit&&(message[at]==' '||message[at]=='\t'))++at;
    if(at>=InputLimit||!message[at])break;
    auto& line=frame.lines[frame.count];size_t used=0,breakAt=0,breakSource=0;
    bool wrapped=false;
    while(at<InputLimit&&message[at]&&message[at]!='\n'&&message[at]!='\r'){
      const unsigned char raw=static_cast<unsigned char>(message[at]);
      const char ch=raw=='\t'?' ':raw>=32&&raw<=126?char(raw):'?';
      if(used==SetupText::Characters){wrapped=true;break;}
      line[used]=ch;line[used+1]=0;
      if(measure(line.data())>SetupText::Width){line[used]=0;wrapped=true;break;}
      if(ch==' '){breakAt=used;breakSource=at;}
      ++used;++at;
    }
    if(wrapped&&breakAt){used=breakAt;line[used]=0;at=breakSource+1;}
    // Every Font2 glyph fits148px. Keep progress bounded even if another caller
    // supplies a narrower/inconsistent measurement function.
    if(wrapped&&!used){++at;}
    while(used&&line[used-1]==' ')line[--used]=0;
    if(at<InputLimit&&(message[at]=='\n'||message[at]=='\r')){
      const char newline=message[at++];
      if(newline=='\r'&&at<InputLimit&&message[at]=='\n')++at;
    }
    ++frame.count;
  }
  while(at<InputLimit&&(message[at]==' '||message[at]=='\t'||message[at]=='\r'||message[at]=='\n'))++at;
  frame.truncated=at==InputLimit||message[at]!=0;
  if(frame.truncated&&frame.count){
    auto& last=frame.lines[frame.count-1];size_t used=std::strlen(last.data());
    while(used>SetupText::Characters-3)last[--used]=0;
    for(;;){
      std::memcpy(last.data()+used,"...",4);
      if(measure(last.data())<=SetupText::Width||!used)break;
      last[--used]=0;
    }
  }
  return frame;
}
}
