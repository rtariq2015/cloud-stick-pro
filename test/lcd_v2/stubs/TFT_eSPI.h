#pragma once
#include "Arduino.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#define TFT_BLACK 0
#define TFT_WHITE 0xffff
#define TL_DATUM 0
#define TR_DATUM 2
inline int FreeSans9pt7b;
namespace lcdmock {
// Actual pinned 2.5.43 glyph data, read-only from the permitted dependency.
#include <Fonts/Font16.c>
#include <Fonts/glcdfont.c>
constexpr int Width=160,Height=80;
struct Call {char kind=0,text[96]{};int x=0,y=0,w=0,h=0,font=0;uint16_t color=0;};
inline std::array<Call,128> calls{};
inline size_t used=0,totalCalls=0,fullClears=0;
inline bool raster=false;
inline std::array<uint16_t,Width*Height> pixels{};
inline int rotation=-1;
inline void clearCalls(){used=0;}
inline void bounds(int x,int y,int w,int h){
  assert(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=Width&&y+h<=Height);
}
inline void record(char kind,int x,int y,int w,int h,uint16_t color,const char* text="",int font=0){
  bounds(x,y,w,h);assert(used<calls.size());auto& call=calls[used++];call={};
  call.kind=kind;call.x=x;call.y=y;call.w=w;call.h=h;call.color=color;call.font=font;
  std::snprintf(call.text,sizeof call.text,"%s",text);++totalCalls;
}
inline void pixel(int x,int y,uint16_t color){bounds(x,y,1,1);if(raster)pixels[y*Width+x]=color;}
inline int glyphWidth(unsigned char c,int selected){
  if(selected==1)return 6;
  assert(selected==2&&c>=32&&c<128);return widtbl_f16[c-32];
}
}
class TFT_eSPI {
 public:
  void init(){}
  void setRotation(int value){lcdmock::rotation=value;}
  void setFreeFont(const void* value=nullptr){freeFont=value!=nullptr;}
  void setTextSize(int size){assert(size==1);}
  void setTextDatum(int value){assert(value==TL_DATUM||value==TR_DATUM);datum=value;}
  void setTextColor(uint16_t fg,uint16_t bg=TFT_BLACK){foreground=fg;background=bg;}
  int textWidth(const char* text,int selected=1){
    int width=0;for(size_t i=0;text[i];++i){assert(i<256);width+=lcdmock::glyphWidth(static_cast<unsigned char>(text[i]),selected);}
    return width;
  }
  int drawString(const String& text,int x,int y,int selected=1){return drawString(text.c_str(),x,y,selected);}
  int drawString(const char* text,int x,int y,int selected=1){
    const int width=textWidth(text,selected),height=selected==2?16:8;
    if(datum==TR_DATUM)x-=width;
    lcdmock::record('T',x,y,width,height,foreground,text,selected);
    if(!lcdmock::raster)return width;
    int cursor=x;
    for(size_t at=0;text[at];++at){
      const auto c=static_cast<unsigned char>(text[at]);const int advance=lcdmock::glyphWidth(c,selected);
      for(int row=0;row<height;++row)for(int col=0;col<advance;++col){
        bool ink=false;
        if(selected==1){if(col<5)ink=lcdmock::font[c*5+col]&(1u<<row);}
        else {const int stride=(advance+6)/8;
          if(col<stride*8)ink=lcdmock::chrtbl_f16[c-32][row*stride+col/8]&(0x80u>>(col%8));}
        lcdmock::pixel(cursor+col,y+row,ink?foreground:background);
      }
      cursor+=advance;
    }
    return width;
  }
  void fillScreen(uint16_t color){++lcdmock::fullClears;lcdmock::record('S',0,0,160,80,color);if(lcdmock::raster)lcdmock::pixels.fill(color);}
  void fillRect(int x,int y,int w,int h,uint16_t color){
    lcdmock::record('R',x,y,w,h,color);if(lcdmock::raster)for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col)lcdmock::pixel(col,row,color);
  }
  void fillRoundRect(int x,int y,int w,int h,int radius,uint16_t color){
    assert(radius>=0&&radius*2<=w&&radius*2<=h);
    lcdmock::record('B',x,y,w,h,color);
    if(lcdmock::raster)for(int row=0;row<h;++row)for(int col=0;col<w;++col){
      const int dx=col<radius?radius-col-1:col>=w-radius?col-(w-radius):0;
      const int dy=row<radius?radius-row-1:row>=h-radius?row-(h-radius):0;
      if(dx*dx+dy*dy<=radius*radius)lcdmock::pixel(x+col,y+row,color);
    }
  }
  void drawFastHLine(int x,int y,int w,uint16_t color){fillRect(x,y,w,1,color);}
  void fillCircle(int x,int y,int radius,uint16_t color){
    lcdmock::record('C',x-radius,y-radius,radius*2+1,radius*2+1,color);
    if(lcdmock::raster)for(int row=-radius;row<=radius;++row)for(int col=-radius;col<=radius;++col)
      if(col*col+row*row<=radius*radius)lcdmock::pixel(x+col,y+row,color);
  }
  void drawLine(int x1,int y1,int x2,int y2,uint16_t color){
    lcdmock::record('L',std::min(x1,x2),std::min(y1,y2),std::abs(x2-x1)+1,std::abs(y2-y1)+1,color);
  }
  void drawBitmap(int x,int y,const unsigned char*,int w,int h,uint16_t color){lcdmock::record('I',x,y,w,h,color);}
 private:
  bool freeFont=false;
  int datum=TL_DATUM;
  uint16_t foreground=TFT_WHITE,background=TFT_BLACK;
};
