# Reference review — 2026-11-03

The owner supplied WebStick and LilyGO links during the 0.3.0 work. They were
reviewed as references; no source from these repositories was copied in this update.

## WebStick

[README](https://github.com/tobychui/WebStick/blob/main/README.md) and
[firmware](https://github.com/tobychui/WebStick/blob/main/firmware/v4.1/web-server/web-server.ino)
describe an ESP8266 SD-backed Wi-Fi file server. Its firmware has no PC-facing
USB MSC implementation. The directory/file interface, network information and
storage view are useful interface references, but do not solve shared PC/MCU FAT
ownership. Its browser upload implementation writes/removes SD files directly;
that architecture is not suitable while our PC owns the same mounted volume.
Our web assets remain self-contained in flash and work without external CDNs.
WebStick backend/frontend licensing is GPLv3; PCB licensing is separate.

## LilyGO T-Dongle S3

The [original board guide](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/docs/en/t-dongle-s3/REAMDE.MD)
confirms ESP32-S3, 16 MB flash, 512 KB SRAM and no PSRAM; display ST7735 160×80.
Our SD pins match D0=14, D1=17, D2=21, D3=18, CLK=12, CMD=16; display pins match
CS=4, MOSI=3, SCLK=5, DC=2, RESET=1, backlight=38 active-low. USB uses TinyUSB.
The vendor supplies separate
[USB mass-storage](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/examples/usb_mass_storage/usb_mass_storage.ino)
and [web filesystem](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/examples/fs_webserver/fs_webserver.ino)
examples. Combining them does not establish safe concurrent FAT access. Our
exclusive SD writer and invalidated read sessions remain necessary.

## RAM and SD storage

[Espressif external-RAM documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/external-ram.html)
describes memory-mapped physical PSRAM attached alongside flash. SD storage cannot
be added to this board's normal heap by enabling Linux-style swap. Application
streaming/offloading is possible; this firmware streams file data in bounded chunks,
keeps web assets in flash, and reads four history rows at a time. Writing a swap
file into the PC-owned SD filesystem would also violate the sole-writer design.

## Continuously powered Wi-Fi

The pinned Arduino setSleep(false) calls esp_wifi_set_ps(WIFI_PS_NONE). The
[Espressif power-save guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html)
describes the throughput/memory/power tradeoffs. Version 0.3.1 disables modem sleep
in station mode because this dongle stays USB-powered; hardware throughput and
minimum heap are being measured, without increasing dynamic buffer allocations.
