#pragma once
#include <cstdint>
#include <string>
#define PROGMEM
#define OUTPUT 1
#define LOW 0
#define TFT_BL 38
using String=std::string;
namespace lcdmock {
inline uint32_t now=0;
inline int backlightPin=-1,backlightValue=-1;
inline bool windowOpen=true;
inline unsigned quiet=0;
}
inline uint32_t millis(){return lcdmock::now;}
inline void pinMode(int pin,int mode){(void)mode;lcdmock::backlightPin=pin;}
inline void digitalWrite(int pin,int value){lcdmock::backlightPin=pin;lcdmock::backlightValue=value;}
