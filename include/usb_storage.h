#pragma once
#include "pro_core.h"
#include "file_incremental_cache.h"
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
struct StorageFileLayout {pro::incremental::Stamp stamp;pro::incremental::Digest digest{};uint64_t size=0;bool clean=false;};
// Fresh FatFS chain proof, not file size/time. Requires the existing read view.
bool storage_file_layout(const std::string&,const pro::incremental::Stamp*,StorageFileLayout&,bool(*cancelled)()=nullptr);
void storage_incremental_invalidate();
#endif
void init_usb_storage(bool alreadyEjected=false);
void poll_storage();
pro::Owner storage_owner();
bool storage_ready();
// Detected/initialized media, independent of the firmware read-view mount.
// A failed card init cannot distinguish an absent card from faulty media.
bool storage_media_detected();
bool storage_begin_read(uint32_t quietSeconds,bool background);
void storage_end_read();
uint32_t storage_quiet_remaining(uint32_t quietSeconds);
uint64_t storage_generation();
bool storage_capacity(uint64_t& total, uint64_t& free);
#if defined(CSP_ENABLE_REMOTE_HEALTH) && CSP_ENABLE_REMOTE_HEALTH
// Read-only, nonblocking health admission. Busy/unknown means active; callers
// cannot equate an invalid/unmounted snapshot with a released FAT registration.
bool storage_read_view_active();
#endif
