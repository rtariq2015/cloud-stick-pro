// Executes the actual pinned FatFS against a synthetic RAM disk. Only this
// fixture generator writes FAT; production still rejects diskWrite with WRPRT.
#include "pro_fat_layout.h"
#include "file_incremental_cache.h"
#include <diskio.h>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>
using namespace pro::incremental;
static std::vector<uint8_t> disk(32*1024*1024);
static Coverage coverage;static uint64_t generation=0;static bool observe=false;
static std::set<uint32_t> fileSectors;static unsigned contentReads=0,writes=0;
extern "C" {
PARTITION VolToPart[FF_VOLUMES]={{0,0},{0,0}};
DWORD get_fattime(){return (DWORD(2026-1980)<<25)|(1u<<21)|(1u<<16);}
int ff_mutex_create(int){return 1;}void ff_mutex_delete(int){}int ff_mutex_take(int){return 1;}void ff_mutex_give(int){}
void* ff_memalloc(unsigned n){return malloc(n);}void ff_memfree(void* p){free(p);}
DSTATUS ff_disk_initialize(BYTE){return 0;}DSTATUS ff_disk_status(BYTE){return 0;}
DRESULT ff_disk_read(BYTE,BYTE* out,LBA_t first,UINT count){
 if(uint64_t(first)+count>disk.size()/512)return RES_PARERR;
 if(observe)for(UINT i=0;i<count;++i)if(fileSectors.count(first+i))++contentReads;
 memcpy(out,disk.data()+size_t(first)*512,size_t(count)*512);return RES_OK;
}
DRESULT ff_disk_write(BYTE,const BYTE* in,LBA_t first,UINT count){
 if(uint64_t(first)+count>disk.size()/512)return RES_PARERR;
 const auto before=coverage.stamp(generation);coverage.wrote(++generation,first,first+count-1);assert(!coverage.clean(before,generation,first,count));++writes;
 memcpy(disk.data()+size_t(first)*512,in,size_t(count)*512);return RES_OK;
}
DRESULT ff_disk_ioctl(BYTE,BYTE command,void* out){switch(command){case CTRL_SYNC:return RES_OK;case GET_SECTOR_COUNT:*static_cast<LBA_t*>(out)=disk.size()/512;return RES_OK;case GET_SECTOR_SIZE:*static_cast<WORD*>(out)=512;return RES_OK;case GET_BLOCK_SIZE:*static_cast<DWORD*>(out)=1;return RES_OK;case CTRL_TRIM:return RES_OK;default:return RES_PARERR;}}
}
struct Layout {
 uint64_t size=0;std::vector<std::pair<uint32_t,uint32_t>> sectors;
 bool begin(uint64_t n,uint64_t,uint64_t,uint64_t,uint64_t){size=n;return true;}
 bool cluster(uint32_t first,uint32_t count){sectors.emplace_back(first,count);return true;}
 bool end(){return true;}
 bool operator==(const Layout& b)const{return size==b.size&&sectors==b.sectors;}
};
static Layout layout(const char* name,bool expected=true){FIL file{};assert(f_open(&file,name,FA_READ)==FR_OK);Layout out;assert(walkFatLayout(file,out)==expected);assert(f_close(&file)==FR_OK);return out;}
static void writeFile(const char* name,unsigned bytes,uint8_t value){FIL file{};assert(f_open(&file,name,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);uint8_t block[2048];memset(block,value,sizeof block);while(bytes){UINT n=0,take=std::min(bytes,unsigned(sizeof block));assert(f_write(&file,block,take,&n)==FR_OK&&n==take);bytes-=n;}assert(f_close(&file)==FR_OK);}
static bool clean(const Stamp& old,const Layout& l){for(auto pair:l.sectors)if(!coverage.clean(old,generation,pair.first,pair.second))return false;return true;}
int main(){
 std::array<uint8_t,16> boot{};boot[0]=3;coverage.start(boot);uint8_t work[4096];MKFS_PARM options{};options.fmt=FM_FAT|FM_SFD;options.n_fat=2;options.n_root=128;options.au_size=2048;
 assert(f_mkfs("0:",&options,work,sizeof work)==FR_OK);FATFS fs{};assert(f_mount(&fs,"0:",1)==FR_OK);assert(fs.csize==4&&fs.ssize==512);
 writeFile("0:/OLD.PDF",64*1024,0x41);const auto original=layout("0:/OLD.PDF");
 for(auto range:original.sectors)for(unsigned i=0;i<range.second;++i)fileSectors.insert(range.first+i);
 coverage.seal(generation);const auto old=coverage.stamp(generation);observe=true;contentReads=0;const auto baselineWrites=writes;
 assert(layout("0:/OLD.PDF")==original);assert(!contentReads&&writes==baselineWrites);
 writeFile("0:/NEW.PDF",8192,0x42);assert(layout("0:/OLD.PDF")==original&&clean(old,original)&&!contentReads);
 // Genuine data edit with unchanged size AND restored FAT modification time.
 FILINFO info{};assert(f_stat("0:/OLD.PDF",&info)==FR_OK);FIL file{};assert(f_open(&file,"0:/OLD.PDF",FA_WRITE)==FR_OK);UINT n;uint8_t value=0x43;assert(f_write(&file,&value,1,&n)==FR_OK&&n==1);assert(f_close(&file)==FR_OK);assert(f_utime("0:/OLD.PDF",&info)==FR_OK);
 assert(layout("0:/OLD.PDF")==original&&!clean(old,original));FILINFO after{};assert(f_stat("0:/OLD.PDF",&after)==FR_OK&&after.fsize==info.fsize&&after.fdate==info.fdate&&after.ftime==info.ftime);
 // Fragmented ascending chain: exact public FatFS still reconstructs it.
 FIL a{},b{};assert(f_open(&a,"0:/FRAG.PDF",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);assert(f_open(&b,"0:/GAPS.BIN",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
 uint8_t block[2048]{};for(unsigned i=0;i<8;++i){assert(f_write(&a,block,sizeof block,&n)==FR_OK&&n==sizeof block);assert(f_write(&b,block,sizeof block,&n)==FR_OK&&n==sizeof block);}assert(f_close(&a)==FR_OK&&f_close(&b)==FR_OK);const auto fragmented=layout("0:/FRAG.PDF");assert(fragmented.sectors.size()==8);assert(fragmented.sectors[1].first>fragmented.sectors[0].first+4);
 assert(f_rename("0:/FRAG.PDF","0:/RENAMED.PDF")==FR_OK);assert(layout("0:/RENAMED.PDF")==fragmented);
 assert(f_open(&a,"0:/RENAMED.PDF",FA_WRITE)==FR_OK);assert(f_lseek(&a,4096)==FR_OK&&f_truncate(&a)==FR_OK&&f_close(&a)==FR_OK);assert(layout("0:/RENAMED.PDF").size==4096);
 writeFile("0:/SHORT.PDF",2049,0x44);layout("0:/SHORT.PDF",false);
 // A cyclic chain is fixture corruption, not a new filesystem decoder.
 assert(fs.fs_type==FS_FAT16);assert(f_open(&a,"0:/OLD.PDF",FA_READ)==FR_OK);DWORD first=a.obj.sclust;assert(f_close(&a)==FR_OK);assert(f_mount(nullptr,"0:",0)==FR_OK);
 for(unsigned fat=0;fat<fs.n_fats;++fat){const size_t pos=(size_t(fs.fatbase)+fat*fs.fsize)*512+first*2;disk[pos]=first&255;disk[pos+1]=first>>8;}
 assert(f_mount(&fs,"0:",1)==FR_OK);layout("0:/OLD.PDF",false);assert(f_mount(nullptr,"0:",0)==FR_OK);
 observe=false;disk.assign(64*1024*1024,0);options.fmt=FM_FAT32|FM_SFD;options.au_size=512;assert(f_mkfs("0:",&options,work,sizeof work)==FR_OK);assert(f_mount(&fs,"0:",1)==FR_OK&&fs.fs_type==FS_FAT32);writeFile("0:/FAT32.PDF",65536,0x45);assert(layout("0:/FAT32.PDF").size==65536);assert(f_mount(nullptr,"0:",0)==FR_OK);
 std::cout<<"PASS actual pinned FatFS read-only aligned layout: FAT16/FAT32/regular/fragmented/edit/new/rename/truncate/short/cycle; FIL="<<sizeof(FIL)<<" Coverage="<<sizeof(Coverage)<<" bytes; old-content reads on new-file walk=0\n";
}
