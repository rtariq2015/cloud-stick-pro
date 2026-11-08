# Desktop Setup 0.6.1: ROM recovery-read correction

During the private Bluetooth diagnostic installation, Setup 0.6.0 stopped its
first recovery copy at the ROM loader’s 2 MiB default boundary. The stick has
16 MiB of flash. No firmware had been written; the recovery requirement correctly
prevented installation from proceeding with an incomplete copy.

Setup 0.6.1 sends the vendor’s SPI_SET_PARAMS command after proving the exact
ESP32-S3, 16 MiB, no-PSRAM board and its unchanged security state. This sets a
RAM descriptor used by ROM reads; it does not program flash, alter an eFuse,
change the partition layout or upload a stub. Unknown boards fail before this
command. A parameter error closes serial and prevents recovery/install work.

A read-only physical test on the owner’s stick reproduced failure at offset
0x200000 before configuration, then successfully read that boundary and the last
64 bytes of the 16 MiB flash after configuration. It ran in the actual no-swap
systemd scope, with no serial reset or flash-data write. No private bytes, device
identifiers, keys or raw output are published. Full fresh encrypted recovery,
protected application update and post-boot acceptance remain pending at this
source milestone; a read probe is not that acceptance.

Regression tests exercise the real ROMDevice constructor with a fake ROM whose
geometry begins at 2 MiB, including proof ordering, both read boundaries, denied
board and failed parameter setup. Existing encrypted recovery, known-preimage,
app-only write, full preservation/readback and uncertain-write protections remain.
Native Linux/Windows package checks are separate from physical Windows USB and
new-board acceptance. The diagnostic firmware is private and remains separate
from this desktop installer correction.

Vendor reference: [Espressif ESP32-S3 serial protocol](https://docs.espressif.com/projects/esptool/en/release-v4/esp32s3/advanced-topics/serial-protocol.html#spi-set-parameters).
Keep [Credits and the source/binary release gate](../CREDITS.md).
