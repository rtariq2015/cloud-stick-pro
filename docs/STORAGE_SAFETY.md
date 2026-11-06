# Storage behavior in 0.2.0

## Dongle-only background mode

The computer is the only writer to the microSD filesystem. Firmware initializes
SD/MMC without mounting FAT, exports raw USB MSC sectors, and stores all settings,
receipts and logs in internal NVS/LittleFS. Its FatFS disk-write callback always
returns `RES_WRPRT`.

After a configurable quiet period (120 seconds by default), the backup task
registers a fresh read-only FatFS/VFS mount. Every physical SD access shares one
mutex. Before any host sector write, the USB callback advances a generation
counter and records its time. This invalidates an in-progress reader before the
write begins. Firmware disk reads then fail; cached-file streaming/hash operations
also check the session guard. The worker closes its files, discards the whole
mount and retries with a new mount after the next quiet period. Host reads do not
invalidate a session. Firmware never writes FAT metadata or changes host ownership
merely to take a backup.

The nightly option uses local time and a configurable window, including windows
that cross midnight. The default is 1–5 am Eastern time, with DST rules. Quiet-time
checks still apply. A file already uploading may finish after the end hour. Valid
network time is required for HTTPS. Eject-only mode retains the old exclusive
read-only workflow for hosts that need that choice.

## What this guarantees, and what it cannot see

This is a generation-checked read session, **not copy-on-write storage** and not an
application-consistent snapshot. It prevents knowingly mixing physical SD reads
across USB writes. A host can buffer writes in RAM, preallocate a file, or pause
mid-save longer than the quiet interval. The dongle cannot see unsaved memory or
infer file-close events from USB block traffic. It may back up that intermediate
on-card version; a later saved change is hashed and uploaded on a later pass.
Increasing the quiet interval or using an overnight window reduces this exposure.
Frequently changing media can postpone a complete pass indefinitely.

Raw SD I/O errors or an unexpected USB stop while host-owned latch a fault and
hide the medium. Mount failure never formats or repairs the card. FAT and the SD
controller cannot guarantee integrity through power loss during host writes.
No backup runs without USB power. Eject still releases host ownership; it is not
needed for ordinary automatic backup, but remains required before firmware
installation or a settings-changing restart.

## Cloud publication and recovery

Each pending source is hashed, journaled internally, then uploaded to a reserved
per-device `._csp_uploads_<device-id>/` staging collection. Source paths using that
root prefix are rejected. The staged object is downloaded and checked against the
source SHA-256 and length. Only then is it moved to the normal destination.

For an absent destination, MOVE uses `Overwrite: F`. For an existing destination,
a bounded Depth:0 PROPFIND must identify one ordinary file and a strong ETag.
Collections, unknown types, ambiguous XML or missing validators fail closed.
MOVE uses a tagged WebDAV `If` condition for that destination ETag, protecting
against a change between inspection and replacement. The final destination is
then downloaded and checked again before recording success. A write arriving
around publication can leave a previously validated on-card version in the cloud;
the next quiet pass picks up the newer version. It cannot turn a partial staging
upload into a successful verification.

This relies on the server implementing WebDAV conditions and MOVE correctly;
unsupported servers fail visibly. Abandoned staging objects can remain after
power loss/source removal. They are distinct from published backups. There is no
automatic deletion of local files, remote folders or unrelated cloud objects.

The internal journal stores two CRC-protected 512-byte generations per path,
with checked temporary write, flush/fsync, readback and rename. Full 256-bit keys
use lossless base32 filenames so the complete name fits LittleFS's 64-byte limit.
Corrupt receipts requeue conservatively. If a MOVE completed before a reset,
verification can recover from the final path when staging is absent. Existing
verified receipts refer to a past successful readback; they do not detect later
cloud-side deletion or modification.

## Evidence

See the [public project history](PROJECT_HISTORY.md) and its dated release
references for image-specific scope. This guide retains the original storage
invariants; it is not a current-image physical acceptance record. Host unit
tests alone do not establish USB timing, power-cut safety, server durability
or compatibility with every host filesystem/cache policy.
