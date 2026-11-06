// Desktop-only actual Store/COW2 + pinned Espressif Security2 test adapter.
// Synthetic fixture arrays only. No radio, USB, NVS, file or network interfaces.
#include <owner_login_store.h>
#include <owner_record.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
extern "C" {
#include "protocomm_security2.h"
}
#include "synthetic_owner_records.h"
namespace {
constexpr uint8_t Mac[6]={0,0x11,0x22,0x33,0x44,0x55};
struct SyntheticStore final:pro::wifi_setup::BlobStore {
  pro::owner_login::Bytes bytes;bool barrier=false;unsigned writes=0;
  SyntheticStore(){std::memcpy(bytes.value.data(),OldRecord,bytes.value.size());}
  pro::wifi_setup::Read read(const char* key,uint8_t* out,size_t cap,size_t& n)override{
    assert(!std::strcmp(key,"owner")&&cap==bytes.value.size());n=cap;std::memcpy(out,bytes.value.data(),cap);return pro::wifi_setup::Read::Found;}
  bool write(const char* key,const uint8_t* in,size_t n)override{
    assert(barrier&&!std::strcmp(key,"owner")&&n==bytes.value.size());++writes;std::memcpy(bytes.value.data(),in,n);return true;}
};
bool authorized(void*){return true;}
bool arm(void* context){static_cast<SyntheticStore*>(context)->barrier=true;return true;}
int fromHex(const char* text,uint8_t* out,size_t cap){
  const size_t n=std::strlen(text);if(n%2||n/2>cap)return -1;
  for(size_t i=0;i<n;i+=2){unsigned value=0;for(size_t j=0;j<2;++j){const char c=text[i+j];const unsigned d=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):16;if(d==16)return -1;value=value*16+d;}out[i/2]=uint8_t(value);}return int(n/2);
}
}
int main(){
  using namespace pro::owner_login;SyntheticStore raw;Store store(raw,Mac);Current current;
  pro::wifi_setup::OwnerRecord active;protocomm_security2_params_t parameters{};
  auto select=[&]{assert(store.load(current)==Loaded::Ready);assert(pro::wifi_setup::decodeOwner(current.bytes.value.data(),RecordSize,Mac,active));
    parameters.salt=reinterpret_cast<const char*>(active.salt.data());parameters.salt_len=active.salt.size();
    parameters.verifier=reinterpret_cast<const char*>(active.verifier.data());parameters.verifier_len=active.verifier.size();};
  select();protocomm_security_handle_t handle=nullptr;
  assert(protocomm_security2.init(&handle)==ESP_OK&&protocomm_security2.new_transport_session(handle,1)==ESP_OK);
  char line[4096];uint8_t input[1024];
  while(std::fgets(line,sizeof line,stdin)){
    line[std::strcspn(line,"\r\n")]=0;char* payload=std::strchr(line,' ');if(payload)*payload++=0;else payload=const_cast<char*>("");
    uint8_t* output=nullptr;ssize_t length=0;int error=ESP_ERR_INVALID_ARG;const int n=fromHex(payload,input,sizeof input);
    if(!std::strcmp(line,"session")&&n>0)error=protocomm_security2.security_req_handler(handle,&parameters,1,input,n,&output,&length,nullptr);
    else if(!std::strcmp(line,"encrypt")&&n>=0&&n<=1008)error=protocomm_security2.encrypt(handle,1,input,n,&output,&length);
    else if(!std::strcmp(line,"decrypt")&&n>=16)error=protocomm_security2.decrypt(handle,1,input,n,&output,&length);
    else if(!std::strcmp(line,"rotate")){
      Bytes candidate;std::memcpy(candidate.value.data(),NewRecord,RecordSize);uint64_t revision=0;
      assert(store.publish(current,candidate,current.revision,authorized,nullptr,revision,arm,&raw)==Saved::Committed);
      assert(revision&&raw.writes==1&&raw.barrier);select();
      assert(protocomm_security2.close_transport_session(handle,1)==ESP_OK);
      error=protocomm_security2.new_transport_session(handle,1);raw.barrier=false;
    }else if(!std::strcmp(line,"reset")){
      assert(protocomm_security2.close_transport_session(handle,1)==ESP_OK);error=protocomm_security2.new_transport_session(handle,1);
    }else if(!std::strcmp(line,"quit"))break;
    std::printf("{\"error\":%d,\"data\":\"",error);
    if(error==ESP_OK&&output&&length>=0&&length<=1024)for(ssize_t i=0;i<length;++i)std::printf("%02x",output[i]);
    std::printf("\",\"writes\":%u}\n",raw.writes);std::fflush(stdout);std::free(output);
  }
  const auto cleanup=protocomm_security2.cleanup(handle);pro::wifi_setup::eraseSecretMemory(&active,sizeof active);
  return cleanup==ESP_OK?0:1;
}
