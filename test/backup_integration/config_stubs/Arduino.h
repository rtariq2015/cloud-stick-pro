#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#define PROGMEM
class String:public std::string {
 public:
  using std::string::string;
  String()=default;
  String(const std::string& s):std::string(s){}
  bool isEmpty()const{return empty();}
  int indexOf(char c)const{auto pos=find(c);return pos==npos?-1:int(pos);}
  bool endsWith(const char* s)const{const auto n=std::strlen(s);return size()>=n&&compare(size()-n,n,s)==0;}
  bool startsWith(const char* s)const{return rfind(s,0)==0;}
  size_t write(uint8_t c){push_back(char(c));return 1;}
  size_t write(const uint8_t* data,size_t size){append(reinterpret_cast<const char*>(data),size);return size;}
};
