#pragma once
#include <ff.h>
#include <cstdint>
namespace pro::incremental {
// The pinned FF_USE_FASTSEEK=0 f_lseek follows the real FAT chain. A sector-
// aligned read-only seek never fills the per-file data cache (ff.c4477–4631).
// No FAT bytes are decoded here. Unsupported descending chains and short final
// clusters safely fall back rather than reading old contents to infer a layout.
template<class Visitor> bool walkFatLayout(FIL& file,Visitor& visitor){
  FATFS* fs=file.obj.fs;const uint64_t size=f_size(&file);
  if(!fs||!(file.flag&FA_READ)||(file.flag&FA_WRITE)||fs->fs_type<FS_FAT12||fs->fs_type>FS_FAT32||!fs->csize||fs->ssize!=512||fs->n_fatent<3)return false;
  const uint64_t clusterBytes=uint64_t(fs->csize)*512;
  const uint64_t count=size/clusterBytes+(size%clusterBytes!=0);
  if(count>uint64_t(fs->n_fatent)-2||!visitor.begin(size,fs->database,fs->csize,fs->n_fatent,fs->volbase))return false;
  uint32_t previous=0;
  for(uint64_t index=0;index<count;++index){
    uint32_t cluster=file.obj.sclust;
    if(index){const uint64_t offset=index*clusterBytes+512;
      if(offset>size||offset>uint64_t(FSIZE_t(-1))||f_lseek(&file,FSIZE_t(offset))!=FR_OK||f_tell(&file)!=offset)return false;
      cluster=file.clust;
    }
    if(cluster<2||cluster>=fs->n_fatent||cluster<=previous)return false;
    const uint64_t first=uint64_t(fs->database)+uint64_t(cluster-2)*fs->csize;
    const uint64_t remaining=size-index*clusterBytes;
    const uint32_t sectors=uint32_t((remaining<clusterBytes?remaining:clusterBytes)+511)/512;
    if(first>UINT32_MAX||first+sectors>uint64_t(UINT32_MAX)+1||!visitor.cluster(uint32_t(first),sectors))return false;
    previous=cluster;
  }
  return visitor.end();
}
}
