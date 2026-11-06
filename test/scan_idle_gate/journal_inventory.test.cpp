// Actual receipt I/O/format on disposable synthetic POSIX files. No SD/NVS.
#include "pro_journal.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
int main(int argc,char** argv){
  assert(argc==2);const std::string root=argv[1];assert(std::filesystem::create_directory(root));
  pro::FileJournal journal(root);
  for(unsigned i=0;i<4;++i){
    pro::Record record;record.path=i?"OLD-SYNTHETIC/removed-"+std::to_string(i)+".pdf":"QA/current.pdf";
    record.destination=std::string(64,i==1?'b':'a');record.digest=std::string(64,'c');
    record.sequence=1;record.size=19;record.verifiedAt=1767225600;
    record.state=i==2?pro::State::Pending:i==3?pro::State::Transferred:pro::State::Verified;
    assert(journal.save(std::string(64,char('1'+i)),record));
  }
  // Legacy 64-hex filenames are load fallbacks on supported builds, but next()
  // enumerates only current52-character base32 stems. Do not blame a mere legacy
  // orphan for the demonstrated rejection of an enumerated canonical row.
  pro::Record legacy;legacy.path="OLD-SYNTHETIC/legacy.pdf";legacy.destination=std::string(64,'b');
  legacy.digest=std::string(64,'c');legacy.sequence=1;legacy.state=pro::State::Pending;
  uint8_t encoded[pro::RecordSize];assert(pro::encodeRecord(legacy,encoded));
  const auto legacyName=root+"/"+std::string(64,'9')+".0";
  FILE* out=std::fopen(legacyName.c_str(),"wb");assert(out);
  assert(std::fwrite(encoded,1,sizeof encoded,out)==sizeof encoded);assert(std::fclose(out)==0);
  pro::Record loaded;assert(journal.load(std::string(64,'9'),loaded)==pro::Load::Found);
  unsigned found=0,foreign=0,pending=0,transferred=0;std::string cursor,next;
  for(;;){auto result=journal.next(cursor,next,loaded);if(result==pro::Load::Missing)break;
    assert(result==pro::Load::Found);cursor=next;++found;
    foreign+=loaded.destination==std::string(64,'b');pending+=loaded.state==pro::State::Pending;
    transferred+=loaded.state==pro::State::Transferred;
    assert(loaded.path!="OLD-SYNTHETIC/legacy.pdf");
  }
  assert(found==4&&foreign==1&&pending==1&&transferred==1);
  const auto corruptName=root+"/"+pro::journalStem(std::string(64,'2'))+".1";
  out=std::fopen(corruptName.c_str(),"r+b");assert(out);assert(std::fputc('X',out)!=EOF);assert(std::fclose(out)==0);
  cursor.clear();unsigned corrupt=0;found=0;
  for(;;){auto result=journal.next(cursor,next,loaded);if(result==pro::Load::Missing)break;
    assert(result==pro::Load::Found||result==pro::Load::Corrupt);cursor=next;
    corrupt+=result==pro::Load::Corrupt;found+=result==pro::Load::Found;
  }
  assert(corrupt==1&&found==3);
  std::cout<<"PASS actual FileJournal::next globally enumerates foreign/Pending/Transferred canonical rows, ignores66-byte legacy names, and reports corrupt canonical rows\n";
}
