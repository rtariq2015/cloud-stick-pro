#pragma once
#include <cstdint>

// Optional runtime-only power policy. Existing image identities and normal
// production builds retain their original SDK policy and do not call this path.
#ifndef CSP_BACKUP_WIFI_AWAKE
#define CSP_BACKUP_WIFI_AWAKE 0
#endif
#if CSP_BACKUP_WIFI_AWAKE != 0 && CSP_BACKUP_WIFI_AWAKE != 1
#error CSP_BACKUP_WIFI_AWAKE must be 0 or 1
#endif
#if CSP_BACKUP_WIFI_AWAKE && (!CSP_ENABLE_BACKUP_SETUP || !CSP_ENABLE_WIFI_SETUP || !CSP_ENABLE_BLE_MANAGEMENT || !CSP_PRODUCT_RC_PROFILE || (!CSP_PRODUCT_RC2_PROFILE && !CSP_PRODUCT_SPEED2_PROFILE && !CSP_PRODUCT_RC3_PROFILE && !CSP_PRODUCT_RC4_PROFILE && !CSP_PRODUCT_RC5_PROFILE && !CSP_PRODUCT_SETUP_DEV_PROFILE))
#error Managed Wi-Fi awake policy requires a distinct combined product or setup development image
#endif

namespace pro {
// Owner-loop only. No allocation, radio lifetime operation, file/configuration
// access, timer or thread. SDK values NONE/MIN/MAX are checked in the adapter.
class BackupWifiPower {
 public:
  struct Driver {
    void* context;
    int (*read)(void*,int&);
    int (*write)(void*,int);
  };
  enum class Fault:uint8_t {
    None,InvalidDriver,ReadPrevious,InvalidPrevious,SetAwake,ReadAwake,
    AwakeMismatch,SetPrevious,ReadRestored,RestoreMismatch
  };

  bool acquire(const Driver& driver){
    if(faulted())return false;
    if(owned_)return true;
    if(!driver.read||!driver.write)return fail(Fault::InvalidDriver,0);
    int observed=-1;
    int code=driver.read(driver.context,observed);
    if(code)return fail(Fault::ReadPrevious,code);
    if(observed<0||observed>2)return fail(Fault::InvalidPrevious,0);
    previous_=observed;owned_=true;
    if(previous_==0)return true; // The capture itself proves NONE already.
    code=driver.write(driver.context,0);
    if(code)return fail(Fault::SetAwake,code);
    code=driver.read(driver.context,observed);
    if(code)return fail(Fault::ReadAwake,code);
    if(observed!=0)return fail(Fault::AwakeMismatch,0);
    return true;
  }

  // Drained is insufficient: only the caller's fully reaped Stopped proof may
  // pass reclaimed=true. A pending worker performs no SDK operation here.
  bool release(const Driver& driver,bool reclaimed){
    if(faulted())return false;
    if(!owned_)return true;
    if(!reclaimed)return false;
    if(!driver.read||!driver.write)return fail(Fault::InvalidDriver,0);
    if(previous_!=0){
      const int code=driver.write(driver.context,previous_);
      if(code)return fail(Fault::SetPrevious,code);
    }
    int observed=-1;const int code=driver.read(driver.context,observed);
    if(code)return fail(Fault::ReadRestored,code);
    if(observed!=previous_)return fail(Fault::RestoreMismatch,0);
    owned_=false;previous_=-1;return true;
  }
  bool owned()const{return owned_;}
  bool faulted()const{return fault_!=Fault::None;}
  Fault fault()const{return fault_;}
  int error()const{return error_;}
  int previous()const{return previous_;}

 private:
  bool fail(Fault fault,int error){fault_=fault;error_=error;return false;}
  bool owned_=false;
  Fault fault_=Fault::None;
  int previous_=-1,error_=0;
};
}
