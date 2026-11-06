#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

// Project-local body lifetime protection for the pinned HTTPClient framing
// implementation. No asynchronous task/timer touches TLS; the owner polls this
// through the client's existing virtual read interface. This does not replace
// certificate/hostname validation, alter HTTP framing, or reconfigure timeouts.
namespace pro::http_response {
class Guard {
 public:
  using Cancelled=bool (*)(void*);
  static constexpr int CancelledError=-2010,DeadlineError=-2012;
  Guard(Cancelled cancel,void* context,uint32_t totalStartedMs,uint32_t totalLimitMs,
      uint32_t idleLimitMs=15000):started_(totalStartedMs),last_(totalStartedMs),
      totalLimit_(totalLimitMs),idleLimit_(idleLimitMs),cancel_(cancel),context_(context){}
  // Begins only the body's inactivity clock. A caller's total-attempt deadline
  // is never restarted merely because headers have arrived.
  void arm(){last_=millis();}
  bool ready(){
    if(error_)return false;
    if(cancel_&&cancel_(context_))error_=CancelledError;
    else {const uint32_t now=millis();if(uint32_t(now-started_)>=totalLimit_||uint32_t(now-last_)>=idleLimit_)error_=DeadlineError;}
    return error_==0;
  }
  void progress(size_t count){if(count)last_=millis();}
  int error()const{return error_;}
 private:
  uint32_t started_,last_,totalLimit_,idleLimit_;int error_=0;
  Cancelled cancel_;void* context_;
};
class TLS:public WiFiClientSecure {
 public:
  // Guard and its callback context must outlive this TLS client and HTTPClient.
  // Declare them first. Arm after response headers, before writeToStream().
  void arm(Guard& guard){guard_=&guard;guard_->arm();}
  uint8_t connected()override{return ready()?WiFiClientSecure::connected():0;}
  int available()override{return ready()?WiFiClientSecure::available():0;}
  int read(uint8_t* bytes,size_t count)override{
    if(!ready()){if(count)vTaskDelay(1);return -1;}
    const int got=WiFiClientSecure::read(bytes,count);
    // Stream::timedRead and known-length HTTP bodies can spin on nonblocking
    // negative/zero reads. Yield for nonzero requests; no delay is added to
    // positive reads or the base connected() zero-size probe.
    if(count&&got<=0)vTaskDelay(1);
    if(guard_&&got>0)guard_->progress(size_t(got));return got;
  }
  int read()override{uint8_t byte;return read(&byte,1)==1?byte:-1;}
 private:
  Guard* guard_=nullptr;bool stopped_=false;
  bool ready(){if(!guard_||guard_->ready())return true;if(!stopped_){stopped_=true;WiFiClientSecure::stop();}return false;}
};
}
