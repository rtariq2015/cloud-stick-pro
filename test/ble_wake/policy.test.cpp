// SPDX-License-Identifier: MIT
#include "ble_wake.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
using namespace pro::ble_wake;
namespace {
unsigned random_calls=0,verify_calls=0;
bool random(void*,Nonce& nonce){++random_calls;nonce.fill(uint8_t(random_calls));return true;}
bool noRandom(void*,Nonce&){return false;}
bool verify(void*,const PublicKey&,const Message& bytes,const uint8_t* signature,size_t size){
  ++verify_calls;return size==8&&signature[0]==42&&bytes[0]=='C'&&bytes[17]==0&&bytes[18]==7;
}
Pairing pairing(){Pairing p;p.device={1,2,3,4,5,6};p.owner_revision=123;p.key[0]=4;p.key[64]=9;return p;}
Id identity(){Id id{};id[0]=7;return id;}
}
int main(){
  const auto p=pairing();Record bytes{};Pairing loaded;
  assert(encode(p,bytes)&&decode(bytes,p.device,p.owner_revision,loaded));
  assert(loaded.key==p.key&&loaded.owner_revision==p.owner_revision);
  auto other=p.device;other[5]^=1;assert(!decode(bytes,other,123,loaded));
  assert(!decode(bytes,p.device,124,loaded));
  for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;assert(!decode(bad,p.device,123,loaded));}
  auto invalid=p;invalid.owner_revision=0;assert(!encode(invalid,bytes));invalid=p;invalid.key[0]=3;assert(!encode(invalid,bytes));
  Gate gate;Nonce nonce{},again{};uint8_t proof[8]={42};
  assert(gate.challenge(0,123,true,random,nullptr,nonce)==Result::Unavailable);
  gate.bind(p,identity());assert(!gate.paired(124));
  assert(gate.challenge(0,123,false,random,nullptr,nonce)==Result::Busy&&random_calls==0);
  assert(gate.challenge(0,123,true,noRandom,nullptr,nonce)==Result::Unavailable);
  assert(gate.challenge(0,123,true,random,nullptr,nonce)==Result::Ready);
  assert(gate.challenge(1000,123,true,random,nullptr,again)==Result::Ready&&again==nonce&&random_calls==1);
  auto wrong=nonce;wrong[31]^=1;
  assert(gate.authorize(1000,123,true,wrong,proof,8,verify,nullptr)==Result::Denied&&verify_calls==0);
  assert(gate.authorize(1000,123,false,nonce,proof,8,verify,nullptr)==Result::Busy&&verify_calls==0);
  assert(gate.authorize(1000,124,true,nonce,proof,8,verify,nullptr)==Result::Unavailable&&verify_calls==0);
  assert(gate.authorize(1000,123,true,nonce,proof,8,verify,nullptr)==Result::Ready&&verify_calls==1);
  assert(gate.authorize(31000,123,true,nonce,proof,8,verify,nullptr)==Result::Expired&&verify_calls==1);
  assert(gate.challenge(30999,123,true,random,nullptr,again)==Result::Busy);
  assert(gate.challenge(31000,123,true,random,nullptr,again)==Result::Ready&&again!=nonce);
  proof[0]=41;assert(gate.authorize(31000,123,true,again,proof,8,verify,nullptr)==Result::Denied&&verify_calls==2);
  assert(gate.challenge(32999,123,true,random,nullptr,again)==Result::Busy);
  assert(gate.challenge(33000,123,true,random,nullptr,again)==Result::Ready);
  proof[0]=42;assert(gate.authorize(48000,123,true,again,proof,8,verify,nullptr)==Result::Expired&&verify_calls==2);
  gate.bind(p,identity());const uint32_t near=std::numeric_limits<uint32_t>::max()-1000;
  assert(gate.challenge(near,123,true,random,nullptr,nonce)==Result::Ready);
  assert(gate.authorize(500,123,true,nonce,proof,8,verify,nullptr)==Result::Ready);
  assert(gate.challenge(30499,123,true,random,nullptr,again)==Result::Busy);
  assert(gate.challenge(30500,123,true,random,nullptr,again)==Result::Ready);
  gate.clear();assert(!gate.paired(123)&&gate.authorize(40000,123,true,again,proof,8,verify,nullptr)==Result::Unavailable);
  std::cout<<"PASS wake policy: corrupt records, owner/board binding, replay, expiry, forged proof, busy gates, cooldown and rollover; simulated crypto only\n";
}
