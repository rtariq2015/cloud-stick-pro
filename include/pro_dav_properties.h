#pragma once
#include <tinyxml2.h>
#include <string>
#include <cstring>
namespace pro {
// Interpret only DAV names in a bounded Depth:0 response. Unknown resource
// types, weak/missing validators, duplicate properties and collections fail closed.
inline bool davName(const tinyxml2::XMLElement* e,const char* local){
  if(!e)return false;std::string name=e->Name();auto colon=name.find(':');
  if(name.substr(colon==std::string::npos?0:colon+1)!=local)return false;
  std::string attribute=colon==std::string::npos?"xmlns":"xmlns:"+name.substr(0,colon);
  for(auto p=e;p;p=p->Parent()?p->Parent()->ToElement():nullptr){
    if(const char* uri=p->Attribute(attribute.c_str()))return !strcmp(uri,"DAV:");
  }
  return false;
}
inline const tinyxml2::XMLElement* davChild(const tinyxml2::XMLElement* parent,const char* name){
  const tinyxml2::XMLElement* found=nullptr;
  if(parent)for(auto e=parent->FirstChildElement();e;e=e->NextSiblingElement())if(davName(e,name)){if(found)return nullptr;found=e;}
  return found;
}
inline bool strongETag(const std::string& value){
  if(value.size()<2||value.size()>160||value.front()!='"'||value.back()!='"')return false;
  for(size_t i=1;i+1<value.size();++i){unsigned char c=value[i];if(c<33||c>126||c=='"'||c=='['||c==']'||c=='<'||c=='>')return false;}
  return true;
}
inline bool fileProperties(const std::string& xml,std::string& etag){
  etag.clear();if(xml.empty()||xml.size()>4096||xml.find("<!")!=std::string::npos)return false;
  tinyxml2::XMLDocument doc;if(doc.Parse(xml.data(),xml.size())!=tinyxml2::XML_SUCCESS)return false;
  auto root=doc.RootElement();if(!davName(root,"multistatus"))return false;
  auto response=davChild(root,"response");if(!response)return false;
  bool file=false;unsigned types=0,tags=0;
  for(auto ps=response->FirstChildElement();ps;ps=ps->NextSiblingElement()){
    if(!davName(ps,"propstat"))continue;
    auto status=davChild(ps,"status"),prop=davChild(ps,"prop");
    if(!status||!status->GetText()||!prop)return false;
    std::string code=status->GetText();if(code.compare(0,13,"HTTP/1.1 200 ")&&code.compare(0,13,"HTTP/1.0 200 "))continue;
    for(auto p=prop->FirstChildElement();p;p=p->NextSiblingElement()){
      if(davName(p,"resourcetype")){++types;file=!p->FirstChildElement();if(const char* t=p->GetText())if(std::string(t).find_first_not_of(" \t\r\n")!=std::string::npos)file=false;}
      if(davName(p,"getetag")){++tags;if(p->FirstChildElement()||!p->GetText())return false;etag=p->GetText();}
    }
  }
  return file&&types==1&&tags==1&&strongETag(etag);
}
}
