#include "pro_core.h"
#include "file_incremental_cache.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace pro {
bool canRestart(Owner owner,bool backupActive) { return !backupActive && (owner==Owner::Ejected||owner==Owner::Local||owner==Owner::Fault); }
bool validPath(const std::string& p) {
  if (p.empty() || p.size() > PathLimit || p.front() == '/' || p.back() == '/') return false;
  size_t start = 0, depth = 0;
  for (size_t i = 0; i <= p.size(); ++i) {
    if (i == p.size() || p[i] == '/') {
      auto s = p.substr(start, i-start);
      if (s.empty() || s == "." || s == ".." || ++depth > 8) return false;
      if (depth == 1 && (s == ".cloud-stick" || s.compare(0,14,"._csp_uploads_")==0 || s == "System Volume Information" || s == "$RECYCLE.BIN")) return false;
      start = i+1;
    } else {
      unsigned char c = p[i];
      if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
      // Strict UTF-8: reject overlong forms, surrogates, invalid continuations.
      if (c >= 128) {
        size_t n; uint32_t cp;
        if (c >= 0xC2 && c <= 0xDF) { n=1; cp=c&31; }
        else if (c >= 0xE0 && c <= 0xEF) { n=2; cp=c&15; }
        else if (c >= 0xF0 && c <= 0xF4) { n=3; cp=c&7; }
        else return false;
        if (i+n >= p.size()) return false;
        for (size_t k=1; k<=n; ++k) {
          unsigned char d=p[i+k]; if ((d&0xC0)!=0x80) return false; cp=(cp<<6)|(d&63);
        }
        if ((n==2 && cp<0x800) || (n==3 && cp<0x10000) || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF)) return false;
        i += n;
      }
    }
  }
  return true;
}
std::string encodePath(const std::string& path) {
  static const char hex[]="0123456789ABCDEF";
  std::string out; out.reserve(path.size()*3);
  for (unsigned char c : path) {
    if ((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~' || c=='/') out+=char(c);
    else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
  }
  return out;
}
bool validURL(const std::string& s) {
  if (s.size()<10 || s.size()>256 || s.compare(0,8,"https://")!=0) return false;
  size_t end=s.find('/',8); if (end==std::string::npos || end==8) return false;
  for (unsigned char c:s) if (c<=32 || c>=127 || c=='@' || c=='?' || c=='#' || c=='\\') return false;
  // Require a DNS name or IPv4 literal with optional numeric port (no userinfo).
  std::string authority=s.substr(8,end-8);
  size_t colon=authority.find(':');
  std::string host=authority.substr(0,colon);
  if (host.empty()) return false;
  for (char c:host) if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return false;
  if (colon!=std::string::npos) {
    auto port=authority.substr(colon+1); if (port.empty()||port.size()>5) return false;
    uint32_t n=0; for(char c:port) { if(c<'0'||c>'9') return false; n=n*10+c-'0'; }
    if(n==0||n>65535) return false;
  }
  return true;
}
bool validWiFi(const std::string& ssid,const std::string& password,bool open) {
  if(ssid.empty()||ssid.size()>32)return false;
  for(unsigned char c:ssid)if(c<32||c==127)return false;
  if(open)return password.empty();
  if(password.size()<8||password.size()>63)return false;
  for(unsigned char c:password)if(c<32||c==127)return false;
  return true;
}
bool validHex(const char* s) {
  for (size_t i=0;i<64;++i) if (!s[i] || !((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
  return s[64]=='\0';
}
bool validRange(uint64_t sectors,uint32_t lba,uint32_t offset,uint32_t size) {
  if (!sectors || sectors>UINT32_MAX || !size || offset>=512) return false;
  uint64_t pos=uint64_t(lba)*512+offset;
  return pos<uint64_t(sectors)*512 && size<=uint64_t(sectors)*512-pos;
}
uint32_t crc32(const uint8_t* data,size_t n) {
  uint32_t c=~0u; for(size_t i=0;i<n;++i) { c^=data[i]; for(int k=0;k<8;++k)c=(c>>1)^(0xEDB88320u&uint32_t(-int32_t(c&1))); } return ~c;
}
uint32_t retrySeconds(uint32_t attempts) { return std::min<uint32_t>(3600,10u << std::min<uint32_t>(attempts ? attempts-1 : 0,9)); }
static void put32(uint8_t* p,uint32_t n) { for(int i=3;i>=0;--i){p[i]=n&255;n>>=8;} }
static uint32_t get32(const uint8_t* p) { uint32_t n=0;for(int i=0;i<4;++i)n=(n<<8)|p[i];return n; }
static void put64(uint8_t* p,uint64_t n) { put32(p,uint32_t(n>>32));put32(p+4,uint32_t(n)); }
static uint64_t get64(const uint8_t* p) { return (uint64_t(get32(p))<<32)|get32(p+4); }
bool encodeRecord(const Record& r,uint8_t out[RecordSize]) {
  if (!validPath(r.path)||!validHex(r.digest.c_str())||!validHex(r.destination.c_str())||uint8_t(r.state)>2) return false;
  memset(out,0,RecordSize);memcpy(out,"CSP1",4);out[4]=uint8_t(r.state);
  put32(out+8,r.sequence);put32(out+12,r.attempts);put32(out+16,uint32_t(r.error));
  put64(out+20,r.size);put64(out+28,r.next);out[36]=uint8_t(r.path.size());
  memcpy(out+40,r.path.data(),r.path.size());memcpy(out+281,r.digest.data(),64);memcpy(out+346,r.destination.data(),64);
  put64(out+412,r.verifiedAt);put32(out+508,crc32(out,508));return true;
}
bool decodeRecord(const uint8_t in[RecordSize],Record& r) {
  if(memcmp(in,"CSP1",4)||get32(in+508)!=crc32(in,508)||in[4]>2||in[36]>PathLimit) return false;
  Record t;t.path.assign(reinterpret_cast<const char*>(in+40),in[36]);t.digest.assign(reinterpret_cast<const char*>(in+281),64);t.destination.assign(reinterpret_cast<const char*>(in+346),64);
  if(!validPath(t.path)||!validHex(t.digest.c_str())||!validHex(t.destination.c_str())) return false;
  t.state=State(in[4]);t.sequence=get32(in+8);t.attempts=get32(in+12);t.error=int32_t(get32(in+16));t.size=get64(in+20);t.next=get64(in+28);t.verifiedAt=get64(in+412);r=t;return true;
}
bool Backup::persist(const std::string& key,Record& r) { ++r.sequence; return journal_.save(key,r); }
Step Backup::process(const std::string& path,uint64_t now) {
  if(!validPath(path)||!validHex(destination_.c_str()))return {Outcome::Invalid};
  std::string hash;uint64_t size;
#if !CSP_ENABLE_FILE_INCREMENTAL_CACHE
  if(!local_.fingerprint(path,hash,size)||!validHex(hash.c_str()))return {Outcome::LocalError};
#endif
  const std::string key=local_.key(path);
  if(!validHex(key.c_str()))return {Outcome::LocalError};
  Record r;Load loaded=journal_.load(key,r);
  if(loaded==Load::IOError)return {Outcome::MetadataError};
  bool repaired=loaded==Load::Corrupt;
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
  if(loaded==Load::Found&&r.state==State::Verified&&r.path==path&&r.destination==destination_&&validHex(r.digest.c_str())){
    if(local_.reuseVerified(r))return {Outcome::Verified,0,false,false,r.verifiedAt};
  }
  if(!local_.fingerprint(path,hash,size)||!validHex(hash.c_str()))return {Outcome::LocalError};
#endif
  if(loaded!=Load::Found||r.path!=path||r.digest!=hash||r.size!=size||r.destination!=destination_) {
    uint32_t sequence=r.sequence;r=Record();r.sequence=sequence;r.path=path;r.digest=hash;r.size=size;r.destination=destination_;
    if(!persist(key,r))return {Outcome::MetadataError};
  }
  if(r.state==State::Verified){local_.rememberVerified(r);return {Outcome::Verified,0,false,repaired,r.verifiedAt};}
  // Persisted wall-clock retry time; a rolled-back clock can delay at most one hour.
  if(now<r.next && r.next-now<=3600) return {r.state==State::Transferred?Outcome::Transferred:Outcome::Pending,r.error,false,repaired};
  bool transferred=false;
  Result result{true,0};
  if(r.state==State::Pending) {
    result=remote_.put(path,size);
    if(result.ok) {
      transferred=true;r.state=State::Transferred;r.error=0;r.next=0;
      if(!persist(key,r))return {Outcome::MetadataError,0,true,repaired};
    }
  }
  if(result.ok) {
    result=remote_.verify(path,hash,size);
    if(result.ok) {
      r.state=State::Verified;r.error=0;r.attempts=0;r.next=0;r.verifiedAt=clock_?clock_():now;
      if(!persist(key,r))return {Outcome::MetadataError,0,transferred,repaired};
      local_.rememberVerified(r);
      return {Outcome::Verified,0,transferred,repaired,r.verifiedAt};
    }
    // An acknowledged resource can disappear before verification (lost server
    // write/rollback). Retry PUT after backoff, rather than polling GET forever.
    if(result.code==DigestMismatch||result.code==404||result.code==410)r.state=State::Pending;
  }
  if(result.code==SnapshotChanged)return {r.state==State::Transferred?Outcome::Transferred:Outcome::Pending,SnapshotChanged,false,repaired};
  r.error=result.code;if(r.attempts<UINT32_MAX)++r.attempts;
  r.next=(clock_?clock_():now)+retrySeconds(r.attempts);
  if(!persist(key,r))return {Outcome::MetadataError,result.code,transferred,repaired};
  return {Outcome::Failed,result.code,transferred,repaired};
}
}
