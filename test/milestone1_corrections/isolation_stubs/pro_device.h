#pragma once
#include "common.h"
#include "pro_diagnostics.h"
constexpr const char* FirmwareVersion="0.3.3-ble-m1c";
struct BackupStatus {bool active=false;int error=0;char message[96]{};};
BackupStatus backup_status();
void backup_message(const char*,int error=0);
void set_maintenance(bool);
bool maintenance_active();
