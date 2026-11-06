#pragma once
#include "owner_cloud_model.h"
#include <wifi_profile_store.h>
#include <memory>
#include <new>
namespace pro::owner_cloud {
class Blob {
 public:
  ~Blob(){reset();}
  Blob()=default;
  Blob(const Blob&)=delete;
  Blob& operator=(const Blob&)=delete;
  Blob(Blob&& other)noexcept:bytes_(std::move(other.bytes_)),size_(other.size_){other.size_=0;}
  Blob& operator=(Blob&& other)noexcept{if(this!=&other){reset();bytes_=std::move(other.bytes_);size_=other.size_;other.size_=0;}return *this;}
  bool allocate(size_t n){reset();if(!n||n>BlobLimit)return false;bytes_.reset(new(std::nothrow) uint8_t[n]{});if(!bytes_)return false;size_=n;return true;}
  bool trim(size_t n){if(!bytes_||!n||n>size_)return false;pro::wifi_setup::eraseSecretMemory(bytes_.get()+n,size_-n);size_=n;return true;}
  void reset(){if(bytes_)pro::wifi_setup::eraseSecretMemory(bytes_.get(),size_);bytes_.reset();size_=0;}
  uint8_t* data(){return bytes_.get();}
  const uint8_t* data()const{return bytes_.get();}
  size_t size()const{return size_;}
 private:
  std::unique_ptr<uint8_t[]> bytes_;
  size_t size_=0;
};
struct Record {Blob raw;Values initial;Info info;bool missing=true;};
struct PublicSettings {
  uint64_t revision=1;
  std::array<char,129> ntp_server{};
  std::array<char,64> timezone{};
};
enum class Loaded:uint8_t {Valid,Missing,Corrupt,Unavailable};
enum class Saved:uint8_t {Committed,Conflict,Cancelled,Invalid,Unavailable,Unknown,Exhausted};
// The adapter can access only csp/config. No erase, reset, SD or other NVS key.
// CAS compares the entire original raw value as well as cloud_rev. Exact private
// readback is mandatory; failure after the write attempt is Unknown, never rollback.
class Store {
 public:
  Store(pro::wifi_setup::BlobStore& blobs,const char* trusted_ca):blobs_(blobs),ca_(trusted_ca){}
  Loaded load(Record&);
  Saved publish(const Record&,const Values&,const pro::wifi_setup::StoredProfile*,
                uint64_t expected,pro::wifi_setup::StillAuthorized,void*,PublicSettings&);
 private:
  pro::wifi_setup::BlobStore& blobs_;
  const char* ca_;
};
}
