// SPDX-License-Identifier: MIT
#include "lan_wake.h"
#include <cstring>
#include <string_view>
namespace pro::ble_wake {
namespace {
bool same(std::string_view a,std::string_view b){
  if(a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i){char c=a[i];if(c>='A'&&c<='Z')c=char(c+32);if(c!=b[i])return false;}return true;
}
int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
bool hex(std::string_view text,uint8_t* out,size_t size){
  if(text.size()!=size*2)return false;
  for(size_t i=0;i<size;++i){int high=digit(text[2*i]),low=digit(text[2*i+1]);if(high<0||low<0)return false;out[i]=uint8_t(high*16+low);}return true;
}
}
bool request(const char* input,size_t size,const char* host,Frame& out){
  out={};if(!input||!host||size<20||size>1536)return false;std::string_view bytes(input,size);
  for(char c:bytes)if(c!='\r'&&c!='\n'&&(uint8_t(c)<32||uint8_t(c)>126))return false;
  size_t end=bytes.find("\r\n");if(end==std::string_view::npos)return false;
  const auto first=bytes.substr(0,end);Route route=first=="GET /challenge HTTP/1.1"?Route::Challenge:first=="POST /open HTTP/1.1"?Route::Open:Route::Invalid;
  if(route==Route::Invalid)return false;
  bool has_host=false,has_length=false,has_type=false;size_t length=0,at=end+2;
  while(at<size){
    end=bytes.find("\r\n",at);if(end==std::string_view::npos)return false;
    if(end==at){if(end+2!=size)return false;at=end+2;break;}
    const auto row=bytes.substr(at,end-at);for(char c:row)if(c=='\r'||c=='\n')return false;
    const auto colon=row.find(':');if(colon==std::string_view::npos||!colon)return false;
    const auto name=row.substr(0,colon);for(char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'))return false;
    auto value=row.substr(colon+1);if(!value.empty()&&value[0]==' ')value.remove_prefix(1);
    if(same(name,"host")){if(has_host||value!=host)return false;has_host=true;}
    else if(same(name,"content-length")){
      if(has_length||value.empty()||value.size()>3||(value.size()>1&&value[0]=='0'))return false;
      has_length=true;
      for(char c:value){if(c<'0'||c>'9')return false;length=length*10+size_t(c-'0');}if(length>384)return false;
    }else if(same(name,"content-type")){if(has_type||value!="application/json")return false;has_type=true;}
    else if(!(same(name,"accept")||same(name,"accept-encoding")||same(name,"user-agent")||same(name,"connection")||same(name,"cache-control")||same(name,"pragma")||same(name,"accept-language")))return false;
    at=end+2;
  }
  if(!has_host||at!=size||bytes.substr(size-4)!="\r\n\r\n")return false;
  if(route==Route::Challenge&&(has_length||has_type))return false;
  if(route==Route::Open&&(!has_length||!has_type||!length))return false;
  out={route,length,size};return true;
}
bool proof(const char* input,size_t size,Id& id,Nonce& nonce,std::array<uint8_t,72>& signature,size_t& length){
  id={};nonce={};signature={};length=0;if(!input||size>384)return false;
  std::string_view text(input,size);constexpr std::string_view prefix="{\"v\":1,\"id\":\"",between="\",\"nonce\":\"",last="\",\"signature\":\"",suffix="\"}";
  if(text.substr(0,prefix.size())!=prefix)return false;
  text.remove_prefix(prefix.size());
  if(text.size()<IdBytes*2||!hex(text.substr(0,IdBytes*2),id.data(),id.size()))return false;
  text.remove_prefix(IdBytes*2);
  if(text.substr(0,between.size())!=between)return false;
  text.remove_prefix(between.size());
  if(text.size()<NonceBytes*2||!hex(text.substr(0,NonceBytes*2),nonce.data(),nonce.size()))return false;
  text.remove_prefix(NonceBytes*2);
  if(text.substr(0,last.size())!=last)return false;
  text.remove_prefix(last.size());
  if(text.size()<suffix.size()||text.substr(text.size()-suffix.size())!=suffix)return false;
  text.remove_suffix(suffix.size());
  if(text.size()%2||text.size()<16||text.size()>144)return false;
  const auto count=text.size()/2;if(!hex(text,signature.data(),count))return false;length=count;return true;
}
}
