#pragma once
#include "Arduino.h"
#include <netdb.h>
#include <arpa/inet.h>
inline bool dnsHttp=true,dnsHttps=true;
inline unsigned dnsAllocations=0,dnsFrees=0;
inline std::vector<std::string> dnsQueries;
inline std::function<void()> dnsHook;
inline int lwip_getaddrinfo(const char* name,const char* service,const addrinfo* hints,addrinfo** out){
 if(service||hints->ai_family!=AF_INET||hints->ai_socktype!=SOCK_STREAM)throw std::runtime_error("unexpected DNS request");
 dnsQueries.emplace_back(name);if(dnsHook)dnsHook();
 bool good=std::string(name)=="connectivitycheck.gstatic.com"?dnsHttp:std::string(name)=="www.google.com"?dnsHttps:false;
 if(good){*out=new addrinfo{};(*out)->ai_family=AF_INET;auto* address=new sockaddr_in{};address->sin_family=AF_INET;address->sin_addr.s_addr=htonl(0xcb007104);(*out)->ai_addr=reinterpret_cast<sockaddr*>(address);(*out)->ai_addrlen=sizeof(sockaddr_in);++dnsAllocations;return 0;}*out=nullptr;return EAI_FAIL;
}
inline void lwip_freeaddrinfo(addrinfo* p){++dnsFrees;delete reinterpret_cast<sockaddr_in*>(p->ai_addr);delete p;}
