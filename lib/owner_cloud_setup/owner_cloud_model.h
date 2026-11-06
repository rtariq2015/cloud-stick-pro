#pragma once
#include <wifi_setup_model.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace pro::owner_cloud {
// Optional parsing/commit admission only; never weakens the ordinary BLE or
// TLS admission gates. The remaining headroom covers SDK allocator overhead
// beyond bounded raw/CAS/serialization/JSON scratch; target proof is pending.
constexpr size_t OperationFreeFloor=96*1024,OperationLargestFloor=16*1024;
using pro::wifi_setup::Access;
using pro::wifi_setup::Error;
constexpr size_t CredentialLimit=128,FolderLimit=96,BlobLimit=6144;
constexpr uint32_t LifetimeMs=120000;
enum class Field:uint8_t {Username,AppPassword,Folder};
enum class Provider:uint8_t {None,Koofr,Other};
struct Values {
  std::array<char,CredentialLimit+1> username{},password{};
  std::array<char,FolderLimit+1> folder{};
  ~Values();
};
struct Info {
  uint64_t revision=1;
  Provider provider=Provider::None;
  bool configured=false,has_username=false,has_password=false,has_folder=false;
};
bool validCredential(std::string_view,bool username);
bool validFolder(std::string_view);
bool complete(const Values&);
class Candidate {
 public:
  ~Candidate();
  Error begin(const Access&,uint64_t expected,uint64_t actual,uint64_t transaction,
              uint32_t now,const Values&);
  Error stage(const Access&,uint64_t transaction,uint32_t edit,uint32_t now,Field,std::string_view);
  Error prepare(const Access&,uint64_t transaction,uint64_t revision,uint32_t edit,
                uint32_t now,bool accept_unverified,Values&);
  Error abort(const Access&,uint64_t transaction);
  Error status(const Access&,uint32_t now);
  void clear();
  bool editing()const{return active_;}
  uint32_t edit()const{return edit_;}
  uint64_t transaction()const{return transaction_;}
  uint64_t revision()const{return revision_;}
  Info presence()const;
 private:
  Error check(const Access&,uint64_t,uint32_t);
  Values values_;
  uint64_t session_=0,transaction_=0,revision_=0;
  uint32_t started_=0,edit_=0;
  bool active_=false,expired_=false;
};
}
