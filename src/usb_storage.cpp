#include "config.h"
#include "usb_storage.h"
#include "pro_snapshot.h"
#include "pro_blockio.h"
#include <USB.h>
#include <USBMSC.h>
#include <USBCDC.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <ff.h>
#include <diskio_impl.h>
#include <esp_vfs_fat.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <cstring>
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
#include "pro_fat_layout.h"
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <memory>
#include <new>
static pro::incremental::Coverage dirtyCoverage;
static bool coverageStarted=false;
static_assert(sizeof(FIL)<=4352,"Review the pinned FatFS mapping heap budget");
#endif

static USBMSC msc;
static sdmmc_card_t card;
static pro::Ownership ownership;
static SemaphoreHandle_t mutex;
static bool cardReady = false;
static FATFS* fat = nullptr;
static char fatDrive[4];
static pro::SnapshotGuard snapshot;
static bool readMounted=false;
static BYTE registeredDrive=0;
static bool driveRegistered=false;
alignas(4) static uint8_t sector[2048];
struct CardBlocks {
  bool read(uint8_t* data,uint32_t lba,unsigned count){return sdmmc_read_sectors(&card,data,lba,count)==ESP_OK;}
  bool write(const uint8_t* data,uint32_t lba,unsigned count){return sdmmc_write_sectors(&card,data,lba,count)==ESP_OK;}
};

pro::Owner storage_owner() {
  if(!mutex)return pro::Owner::Fault;
  xSemaphoreTake(mutex,portMAX_DELAY); auto s=ownership.state(); xSemaphoreGive(mutex); return s;
}
bool storage_ready() {
  if(!mutex)return false;xSemaphoreTake(mutex,portMAX_DELAY);
  bool ok=cardReady&&readMounted&&snapshot.valid()&&ownership.state()!=pro::Owner::Fault;
  xSemaphoreGive(mutex);return ok;
}
#if defined(CSP_ENABLE_REMOTE_HEALTH) && CSP_ENABLE_REMOTE_HEALTH
bool storage_read_view_active(){
  if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return true;
  const bool active=readMounted||driveRegistered;xSemaphoreGive(mutex);return active;
}
#endif
bool storage_media_detected() {
  if(!mutex)return true; // unknown storage initialization must deny enrollment
  xSemaphoreTake(mutex,portMAX_DELAY);const bool detected=cardReady;
  xSemaphoreGive(mutex);return detected;
}
uint32_t storage_quiet_remaining(uint32_t seconds){
  if(!mutex)return seconds;xSemaphoreTake(mutex,portMAX_DELAY);
  uint32_t ms=ownership.hostIO()?snapshot.remaining(millis(),seconds*1000):0;
  xSemaphoreGive(mutex);return (ms+999)/1000;
}
uint64_t storage_generation(){if(!mutex)return 0;xSemaphoreTake(mutex,portMAX_DELAY);auto value=snapshot.generation();xSemaphoreGive(mutex);return value;}
static int32_t transfer(uint32_t lba,uint32_t offset,void* buffer,uint32_t size,bool write) {
  if(!mutex || !buffer || !pro::validRange(card.csd.capacity,lba,offset,size))return -1;
  if(xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return -1;
  if(!cardReady || !ownership.hostIO()) { xSemaphoreGive(mutex); return -1; }
  if(write){snapshot.wrote(millis());
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
    // Coverage is published BEFORE the first physical sector can change.
    const uint64_t first=uint64_t(lba)+offset/512;
    const uint64_t last=uint64_t(lba)+(uint64_t(offset)+size-1)/512;
    if(size&&last<=UINT32_MAX)dirtyCoverage.wrote(snapshot.generation(),uint32_t(first),uint32_t(last));
    else dirtyCoverage.invalidate();
#endif
  }
  CardBlocks disk;
  bool ok=pro::transferBlocks(disk,lba,offset,static_cast<uint8_t*>(buffer),size,write,sector,sizeof sector);
  if(!ok) { snapshot.invalidate();ownership.fault(); msc.mediaPresent(false);
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
    dirtyCoverage.invalidate();
#endif
  }
  xSemaphoreGive(mutex);return ok?int32_t(size):-1;
}
static int32_t onRead(uint32_t lba,uint32_t offset,void* p,uint32_t n) { return transfer(lba,offset,p,n,false); }
static int32_t onWrite(uint32_t lba,uint32_t offset,uint8_t* p,uint32_t n) { return transfer(lba,offset,p,n,true); }
static bool onStartStop(uint8_t,bool start,bool loadEject) {
  if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
  bool ok=false;
  if(!start && loadEject) {
    ok=ownership.eject() || ownership.state()==pro::Owner::Ejected || ownership.state()==pro::Owner::Local;
    if(ok)msc.mediaPresent(false);
  } else ok=ownership.hostIO();
  xSemaphoreGive(mutex);return ok;
}
static void usbEvent(void*,esp_event_base_t,int32_t id,void*) {
  if(id!=ARDUINO_USB_STOPPED_EVENT || !mutex)return;
  xSemaphoreTake(mutex,portMAX_DELAY);
  // Disconnect/reset is not a clean-eject authorization.
  if(ownership.hostIO()) { snapshot.invalidate();ownership.fault();msc.mediaPresent(false);
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
    dirtyCoverage.invalidate();
#endif
  }
  xSemaphoreGive(mutex);
}
// Firmware FAT is physically read-only at its diskio boundary.
static DSTATUS diskInit(BYTE) { return cardReady ? STA_PROTECT : STA_NOINIT; }
static DRESULT diskRead(BYTE,BYTE* b,DWORD s,UINT n) {
  if(!n || uint64_t(s)+n>card.csd.capacity)return RES_PARERR;
  if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return RES_ERROR;
  bool ok=cardReady&&snapshot.valid()&&ownership.state()!=pro::Owner::Fault;
  if(ok)ok=sdmmc_read_sectors(&card,b,s,n)==ESP_OK;
  xSemaphoreGive(mutex);return ok?RES_OK:RES_ERROR;
}
static DRESULT diskWrite(BYTE,const BYTE*,DWORD,UINT) { return RES_WRPRT; }
static DRESULT diskIoctl(BYTE,BYTE cmd,void* arg) {
  switch(cmd) {
    case CTRL_SYNC:return RES_OK;
    case GET_SECTOR_COUNT:*static_cast<DWORD*>(arg)=card.csd.capacity;return RES_OK;
    case GET_SECTOR_SIZE:*static_cast<WORD*>(arg)=512;return RES_OK;
    case GET_BLOCK_SIZE:*static_cast<DWORD*>(arg)=1;return RES_OK;
    default:return RES_PARERR;
  }
}
void poll_storage() {} // The backup task alone mounts and unmounts its read view.
void storage_end_read(){
  xSemaphoreTake(mutex,portMAX_DELAY);readMounted=false;snapshot.end();xSemaphoreGive(mutex);
  if(driveRegistered){
    f_mount(nullptr,fatDrive,0);
    if(fat){esp_vfs_fat_unregister_path("/sdcard");fat=nullptr;}
    ff_diskio_unregister(registeredDrive);driveRegistered=false;
  }
}
bool storage_begin_read(uint32_t quietSeconds,bool background) {
  if(!mutex||driveRegistered)return false;
  xSemaphoreTake(mutex,portMAX_DELAY);
  bool host=ownership.hostIO();
  bool allowed=cardReady&&ownership.state()!=pro::Owner::Fault&&(!host||background)&&snapshot.begin(millis(),host?quietSeconds*1000:0);
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
  if(allowed){
    // Hardware RNG has its documented RF entropy source only after Wi-Fi is
    // running. Unknown radio state disables reuse, never a fixed/zero nonce.
    wifi_ap_record_t accessPoint{};
    if(!coverageStarted&&esp_wifi_sta_get_ap_info(&accessPoint)==ESP_OK){
      std::array<uint8_t,16> nonce{};esp_fill_random(nonce.data(),nonce.size());
      dirtyCoverage.start(nonce);coverageStarted=true;
    }
    dirtyCoverage.seal(snapshot.generation());
  }
#endif
  xSemaphoreGive(mutex);if(!allowed)return false;
  BYTE pdrv; bool ok=ff_diskio_get_drive(&pdrv)==ESP_OK;
  if(ok) {
    registeredDrive=pdrv;driveRegistered=true;
    snprintf(fatDrive,sizeof fatDrive,"%u:",pdrv);
    const ff_diskio_impl_t impl={diskInit,diskInit,diskRead,diskWrite,diskIoctl};
    ff_diskio_register(pdrv,&impl);
    esp_vfs_fat_conf_t conf={};conf.base_path="/sdcard";conf.fat_drive=fatDrive;conf.max_files=12;
    ok=esp_vfs_fat_register_cfg(&conf,&fat)==ESP_OK;
    if(ok)ok=f_mount(fat,fatDrive,1)==FR_OK;
  }
  xSemaphoreTake(mutex,portMAX_DELAY);ok=ok&&snapshot.valid();
  readMounted=ok;
  if(ownership.state()==pro::Owner::Ejected)ownership.mounted(ok);
  xSemaphoreGive(mutex);
  if(!ok)storage_end_read();return ok;
}
bool storage_capacity(uint64_t& total,uint64_t& free) {
  if(!storage_ready()||!fat)return false;
  DWORD clusters;FATFS* fs;
  if(f_getfree(fatDrive,&clusters,&fs)!=FR_OK)return false;
  total=uint64_t(fs->n_fatent-2)*fs->csize*512;
  free=uint64_t(clusters)*fs->csize*512;return true;
}
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
void storage_incremental_invalidate(){if(!mutex)return;xSemaphoreTake(mutex,portMAX_DELAY);dirtyCoverage.invalidate();xSemaphoreGive(mutex);}
bool storage_file_layout(const std::string& path,const pro::incremental::Stamp* previous,StorageFileLayout& output,bool(*cancelled)()){
  if((cancelled&&cancelled())||!storage_ready()||!pro::validPath(path)||!fat)return false;
  // FIL includes the pinned4KiB cache: checked heap ownership ends here, well
  // before Backup starts TLS. It must never consume4KiB of the worker stack.
  std::unique_ptr<FIL> file(new(std::nothrow) FIL{});if(!file)return false;
  const std::string name=std::string(fatDrive)+"/"+path;
  if(f_open(file.get(),name.c_str(),FA_READ)!=FR_OK)return false;
  StorageFileLayout next;bool ok=true;
  xSemaphoreTake(mutex,portMAX_DELAY);next.stamp=dirtyCoverage.stamp(snapshot.generation());ok=snapshot.valid()&&coverageStarted&&next.stamp.epoch;xSemaphoreGive(mutex);
  struct Visitor {
    StorageFileLayout& out;const pro::incremental::Stamp* old;mbedtls_sha256_context sha;bool(*cancelled)();bool clean=true;uint32_t clusters=0;
    Visitor(StorageFileLayout& value,const pro::incremental::Stamp* before,bool(*stop)()):out(value),old(before),cancelled(stop){mbedtls_sha256_init(&sha);}
    ~Visitor(){mbedtls_sha256_free(&sha);}
    bool number(uint64_t n){uint8_t bytes[8];for(unsigned i=0;i<8;++i)bytes[i]=uint8_t(n>>(i*8));return mbedtls_sha256_update(&sha,bytes,sizeof bytes)==0;}
    bool begin(uint64_t size,uint64_t base,uint64_t clusters,uint64_t count,uint64_t volume){out.size=size;return mbedtls_sha256_starts(&sha,0)==0&&number(size)&&number(base)&&number(clusters)&&number(count)&&number(volume);}
    bool cluster(uint32_t first,uint32_t count){
      if(cancelled&&cancelled())return false;
      // Bounded cooperative work, without holding either the storage mutex or
      // FatFS lock. Layout checks must not starve USB/idle or delay retirement.
      if((++clusters%32)==0)vTaskDelay(1);
      xSemaphoreTake(mutex,portMAX_DELAY);
      const bool valid=snapshot.valid()&&snapshot.generation()==out.stamp.generation&&uint64_t(first)+count<=card.csd.capacity;
      if(old&&!dirtyCoverage.clean(*old,snapshot.generation(),first,count))clean=false;
      xSemaphoreGive(mutex);return valid&&number(first)&&number(count);
    }
    bool end(){out.clean=clean&&(!old||pro::incremental::sameStamp(*old,out.stamp));return mbedtls_sha256_finish(&sha,out.digest.data())==0;}
  } visitor(next,previous,cancelled);
  ok=ok&&pro::incremental::walkFatLayout(*file,visitor);
  if(f_close(file.get())!=FR_OK)ok=false;
  xSemaphoreTake(mutex,portMAX_DELAY);ok=ok&&snapshot.valid()&&snapshot.generation()==next.stamp.generation;xSemaphoreGive(mutex);
  if(cancelled&&cancelled())ok=false;if(ok)output=next;return ok;
}
#endif
void init_usb_storage(bool alreadyEjected) {
  mutex=xSemaphoreCreateMutex();
  if(!mutex) { USBSerial.begin(115200);USB.begin();return; }
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width=4;slot.clk=gpio_num_t(SD_MMC_CLK_PIN);slot.cmd=gpio_num_t(SD_MMC_CMD_PIN);
  slot.d0=gpio_num_t(SD_MMC_D0_PIN);slot.d1=gpio_num_t(SD_MMC_D1_PIN);
  slot.d2=gpio_num_t(SD_MMC_D2_PIN);slot.d3=gpio_num_t(SD_MMC_D3_PIN);
  slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  cardReady=sdmmc_host_init()==ESP_OK && sdmmc_host_init_slot(host.slot,&slot)==ESP_OK && sdmmc_card_init(&host,&card)==ESP_OK;
  cardReady=cardReady && card.csd.sector_size==512 && card.csd.capacity>0;
  if(!cardReady)ownership.fault();
  USBSerial.begin(115200);USB.onEvent(usbEvent);
  msc.vendorID("CSPRO");msc.productID("Cloud Stick Pro");msc.productRevision("0.1");
  msc.onRead(onRead);msc.onWrite(onWrite);msc.onStartStop(onStartStop);
  // No host-visible media is exposed on this safe software restart. Physical
  // reconnect/power loss always starts with the normal USB-owned disk again.
  if(cardReady&&alreadyEjected)ownership.eject();
  msc.mediaPresent(cardReady&&!alreadyEjected);
  if(!msc.begin(cardReady?card.csd.capacity:1,512)) {ownership.fault();msc.mediaPresent(false);}
  USB.begin();
}
