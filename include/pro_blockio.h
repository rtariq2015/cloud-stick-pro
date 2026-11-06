#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace pro {
// Caller validates the complete sector range and serializes physical I/O.
// Bounce storage must be DMA-capable on the device. Partial sectors retain the
// bytes outside the USB request; aligned spans use bounded multi-sector commands.
template<class Disk> bool transferBlocks(Disk& disk,uint32_t lba,uint32_t offset,uint8_t* bytes,uint32_t size,bool write,uint8_t* bounce,size_t capacity){
  if(!bytes||!bounce||!size||offset>=512||capacity<512||capacity%512)return false;
  uint32_t done=0;
  while(done<size){
    if(!offset&&size-done>=512){
      unsigned blocks=unsigned(std::min<size_t>((size-done)/512,capacity/512));size_t count=blocks*512;
      if(write){memcpy(bounce,bytes+done,count);if(!disk.write(bounce,lba,blocks))return false;}
      else{if(!disk.read(bounce,lba,blocks))return false;memcpy(bytes+done,bounce,count);}
      done+=count;lba+=blocks;
    }else{
      uint32_t count=std::min<uint32_t>(512-offset,size-done);
      if(!disk.read(bounce,lba,1))return false;
      if(write){memcpy(bounce+offset,bytes+done,count);if(!disk.write(bounce,lba,1))return false;}
      else memcpy(bytes+done,bounce+offset,count);
      done+=count;++lba;offset=0;
    }
  }
  return true;
}
}
