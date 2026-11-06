#include "owner_cloud_model.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace pro::owner_cloud {
namespace {
template<size_t N>std::string_view view(const std::array<char,N>& value){
  const auto end=std::find(value.begin(),value.end(),char(0));
  return {value.data(),size_t(end-value.begin())};
}
bool utf8(std::string_view value){
  for(size_t i=0;i<value.size();++i){
    const auto c=uint8_t(value[i]);if(c<128)continue;
    unsigned following=0;uint32_t code=0;
    if(c>=0xc2&&c<=0xdf){following=1;code=c&31;}
    else if(c>=0xe0&&c<=0xef){following=2;code=c&15;}
    else if(c>=0xf0&&c<=0xf4){following=3;code=c&7;}
    else return false;
    if(i+following>=value.size())return false;
    for(unsigned n=1;n<=following;++n){const auto next=uint8_t(value[i+n]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63);}
    if((following==2&&code<0x800)||(following==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    i+=following;
  }return true;
}
template<size_t N>void copy(std::array<char,N>& out,std::string_view value){
  pro::wifi_setup::eraseSecretMemory(out.data(),out.size());
  if(!value.empty())std::memcpy(out.data(),value.data(),value.size());
}
}
Values::~Values(){
  pro::wifi_setup::eraseSecretMemory(username.data(),username.size());
  pro::wifi_setup::eraseSecretMemory(password.data(),password.size());
  pro::wifi_setup::eraseSecretMemory(folder.data(),folder.size());
}
bool validCredential(std::string_view value,bool username){
  if(value.empty()||value.size()>CredentialLimit||!utf8(value))return false;
  for(unsigned char c:value)if(c<32||c==127||(username&&c==':'))return false;
  return true;
}
bool validFolder(std::string_view value){
  if(value.empty()||value.size()>FolderLimit)return false;
  bool first=true;
  for(char c:value){
    const bool alnum=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
    if(c=='/'){if(first)return false;first=true;continue;}
    if(first&&!alnum)return false;
    if(!alnum&&c!='.'&&c!='_'&&c!='-')return false;
    first=false;
  }return !first;
}
bool complete(const Values& value){return validCredential(view(value.username),true)&&validCredential(view(value.password),false)&&validFolder(view(value.folder));}
Candidate::~Candidate(){clear();}
void Candidate::clear(){
  copy(values_.username,{});copy(values_.password,{});copy(values_.folder,{});
  session_=transaction_=revision_=0;started_=edit_=0;active_=expired_=false;
}
Error Candidate::begin(const Access& a,uint64_t expected,uint64_t actual,uint64_t transaction,uint32_t now,const Values& initial){
  if(!pro::wifi_setup::Candidate::authorized(a))return Error::Denied;
  if(!a.workers_quiescent)return Error::WorkersBusy;
  if(active_)return Error::Busy;
  if(!actual||!transaction)return Error::Invalid;
  if(expected!=actual)return Error::StaleRevision;
  if(actual==std::numeric_limits<uint64_t>::max())return Error::Exhausted;
  if((!view(initial.username).empty()&&!validCredential(view(initial.username),true))||
     (!view(initial.password).empty()&&!validCredential(view(initial.password),false))||
     (!view(initial.folder).empty()&&!validFolder(view(initial.folder))))return Error::Invalid;
  clear();values_=initial;session_=a.session;transaction_=transaction;revision_=actual;
  started_=now;active_=true;return Error::None;
}
Error Candidate::check(const Access& a,uint64_t transaction,uint32_t now){
  if(!pro::wifi_setup::Candidate::authorized(a)){
    if(a.session&&a.session==session_)clear();return Error::Denied;
  }
  if(!a.workers_quiescent)return Error::WorkersBusy;
  if(a.session!=session_||!transaction||transaction!=transaction_)return Error::StaleTransaction;
  if(expired_)return Error::Expired;
  if(!active_)return Error::StaleTransaction;
  const uint32_t elapsed=now-started_;
  if(elapsed<0x80000000u&&elapsed>=LifetimeMs){
    copy(values_.username,{});copy(values_.password,{});copy(values_.folder,{});
    active_=false;expired_=true;return Error::Expired;
  }
  return Error::None;
}
Error Candidate::stage(const Access& a,uint64_t transaction,uint32_t edit,uint32_t now,Field field,std::string_view value){
  const auto e=check(a,transaction,now);if(e!=Error::None)return e;
  if(edit!=edit_)return Error::StaleTransaction;
  if(edit_==std::numeric_limits<uint32_t>::max())return Error::Exhausted;
  if(field==Field::Username){if(!validCredential(value,true))return Error::Invalid;copy(values_.username,value);}
  else if(field==Field::AppPassword){if(!validCredential(value,false))return Error::Invalid;copy(values_.password,value);}
  else if(field==Field::Folder){if(!validFolder(value))return Error::Invalid;copy(values_.folder,value);}
  else return Error::Invalid;
  ++edit_;return Error::None;
}
Error Candidate::prepare(const Access& a,uint64_t transaction,uint64_t revision,uint32_t edit,uint32_t now,bool accept,Values& out){
  const auto e=check(a,transaction,now);if(e!=Error::None)return e;
  if(revision!=revision_)return Error::StaleRevision;
  if(edit!=edit_)return Error::StaleTransaction;
  if(!accept)return Error::ConfirmationRequired;
  if(!complete(values_))return Error::Invalid;
  out=values_;return Error::None;
}
Error Candidate::abort(const Access& a,uint64_t transaction){
  if(!pro::wifi_setup::Candidate::authorized(a))return Error::Denied;
  if(!a.workers_quiescent)return Error::WorkersBusy;
  if(a.session!=session_||!transaction||transaction!=transaction_)return Error::StaleTransaction;
  clear();return Error::None;
}
Error Candidate::status(const Access& a,uint32_t now){return check(a,transaction_,now);}
Info Candidate::presence()const{
  Info result;result.revision=revision_;result.provider=Provider::Koofr;
  result.has_username=active_&&validCredential(view(values_.username),true);
  result.has_password=active_&&validCredential(view(values_.password),false);
  result.has_folder=active_&&validFolder(view(values_.folder));return result;
}
}
