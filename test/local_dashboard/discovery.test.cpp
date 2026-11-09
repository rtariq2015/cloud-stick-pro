// SPDX-License-Identifier: MIT
#include "local_dashboard.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace pro::local_dashboard;
int main(){
 const uint32_t address=inet_addr("127.0.0.1");const uint16_t http=28470,udp=28471;
 Snapshot snapshot;Server server(http,udp);uint32_t now=1000;
 auto tick=[&](bool allowed=true,bool memory=true){now+=250;server.poll(now,allowed,address,memory,snapshot);};
 int client=::socket(AF_INET,SOCK_DGRAM,0);assert(client>=0);timeval wait{0,50000};assert(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&wait,sizeof wait));
 sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=address;assert(!bind(client,(sockaddr*)&local,sizeof local));
 sockaddr_in target=local;target.sin_port=htons(udp);
 const char query[]="CSP-DISCOVER/1 0123456789abcdef0123456789abcdef";
 auto send=[&](const char* value,size_t size){assert(sendto(client,value,size,0,(sockaddr*)&target,sizeof target)==ssize_t(size));};
 char reply[128]{};
 tick();send(query,strlen(query));tick();assert(recv(client,reply,sizeof reply,0)==44);assert(!memcmp(reply,"CSP-FOUND/1 0123456789abcdef0123456789abcdef",44));
 send("CSP-DISCOVER/1 bad",18);tick();assert(recv(client,reply,sizeof reply,0)<0);
 send(query,strlen(query)+1);tick();assert(recv(client,reply,sizeof reply,0)<0);
 // A zero/unknown subnet cannot authorize any broadcast reply.
 send(query,strlen(query));server.poll(now+=250,true,address,true,snapshot,0);assert(recv(client,reply,sizeof reply,0)<0);
 tick(false);assert(!server.listening());send(query,strlen(query));tick(false);assert(recv(client,reply,sizeof reply,0)<0);
 tick();tick(true,false);assert(!server.listening());
 for(unsigned i=0;i<100;i++){tick();assert(server.listening());assert(server.stop());int probe=socket(AF_INET,SOCK_DGRAM,0);assert(probe>=0);assert(!bind(probe,(sockaddr*)&target,sizeof target));close(probe);}
 close(client);puts("PASS actual UDP nonce/length/subnet/memory gates and 100 descriptor-release cycles");
}
