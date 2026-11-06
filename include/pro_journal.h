#pragma once
#include "pro_core.h"
namespace pro {
// Lossless base32 of the 256-bit key: 52 characters, plus a 2-character suffix.
// The target's LittleFS filename limit is 64, so 64 hex characters + '.0' fails.
std::string journalStem(const std::string& key);
class FileJournal : public Journal {
 public:
  explicit FileJournal(std::string root):root_(root){}
  Load load(const std::string&,Record&) override;
  bool save(const std::string&,const Record&) override;
  // Bounded, lexicographic receipt enumeration; no SD reads or heap-sized index.
  Load next(const std::string& after,std::string& cursor,Record& record);
 private:
  std::string root_;
  bool room(const std::string& key);
};
}
