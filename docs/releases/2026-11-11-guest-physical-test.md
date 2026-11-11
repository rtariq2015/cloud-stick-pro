# Guest Wi-Fi joins; internet access remains unverified — 11 November 2026

The original LILYGO T-Dongle S3 joined **Guest-Net** directly on its first attempt.
It received a valid DHCP lease and gateway, and resolved both diagnostic hostnames.
This proves association and local network configuration without a Raspberry Pi.
It does **not** yet prove usable internet access or cloud backups on Guest-Net.

The HTTP check returned 200 rather than the expected 204. The device clock had not
synchronized, so the diagnostic did not attempt HTTPS. A guest sign-in page or
filtering is possible; the cause has not been confirmed. No response body or
redirect URL was read, and no terms or access controls were submitted.

## Exact diagnostic image and protected installation

| Item | Verified identity or result |
| --- | --- |
| Firmware | `0.4.13-guest-test` |
| Source | `69a3a993a2dd73b57508394dfebb8b37ff14e01f` |
| Application size | 1,679,584 bytes |
| Application SHA-256 | `d33940f61c0ffc0cb9c783f2b415f874e613dbdfecb8462445a6df2249ebdee9` |
| Installation | Owner approved; existing production installer used |
| Recovery | Two fresh independent encrypted 16 MiB reads, reopened and matched before writing |
| Write and readback | Application only; full 16 MiB preservation/readback passed |
| Postboot | Expected firmware, existing owner state and unchanged saved Wi-Fi revision passed |
| Actual worker memory gate | Swap limit and current usage both zero; core dumps disabled |

The protected installation retained the existing internal data outside the
application, including owner/settings and receipt storage. No full-board erase, partition
change, security downgrade or installer-gate relaxation was used. Private recovery
copies, packages, keys and device logs are excluded from publication.

## Actual Guest-Net result

| Check | Physical result |
| --- | --- |
| Visibility and security | Guest-Net visible; Open; 2.4 GHz |
| Signal | RSSI -67 dBm during the connected test |
| Connection | First attempt succeeded |
| DHCP | Valid IPv4 lease and gateway obtained |
| DNS | `connectivitycheck.gstatic.com` and `www.google.com` both resolved |
| HTTP | Status 200; expected 204 was not obtained |
| Clock | Not synchronized |
| HTTPS | Not attempted; status 0 and TLS unverified |
| Captive portal | Possible, not confirmed; no body or redirect URL inspected |
| Internet verdict | **Unverified** |

The probe completed in about 31 seconds without a harness timeout and its task
was reclaimed normally. The firmware's `complete` phase means the checks finished;
it does not by itself mean internet access passed. There was no HTTPS handshake,
so this result must not be described as a certificate-validation failure.

Independent post-test diagnostics confirmed the existing owner and saved Wi-Fi
revision, no store fault, BLE off and the backup worker stopped. Guest-Net remained
a temporary RAM-only connection. **It was not saved as a profile.** Uploads stayed
disabled throughout the diagnostic; no file or cloud upload was performed.

Actual addresses, gateway/DNS server values, radio identities and raw output remain
private. The owner's earlier laptop HTTP204/HTTPS200 results are separate evidence
and do not override this device's result.

## Separate USB and microSD check

After the network probe ended, a separate host read checked six known generated
QA files through USB mass storage. Every file matched its retained SHA-256 value.
No personal file content was read, no files were changed and the full user-file
set was not enumerated. This is a bounded physical USB/microSD readback check,
not a claim that every storage operation or every user file has been tested.

The LCD implementation remains integrated with the existing renderer, but its
physical appearance was not visually accepted in this test. Authenticated BLE
recovery, dashboard restoration, cancellation under a live request and endurance
also remain separate acceptance work. No fix is claimed for the older
[BLE cleanup issue](https://github.com/rtariq2015/cloud-stick-pro/issues/5) or
[cloud readback issue](https://github.com/rtariq2015/cloud-stick-pro/issues/24).

## Normal return — in progress

The owner separately approved returning to normal operation after testing. The
installer's downgrade protection requires a normal successor newer than 0.4.13;
the prepared return image is `0.4.14-return-dev`:

| Item | Prepared return identity |
| --- | --- |
| Source | `aefd1dd1a1f9d7e3db5ca2b9b410cbab061a1511` |
| Application size | 1,704,752 bytes |
| Application SHA-256 | `26a14d0fa7dc42c95379423d93dd42d49da8cf959d82d530a4e6b2b5e7ef5b71` |
| Runtime | Same code and build flags as the previously installed source `c61791697a81e04a3738533b05dcd70e475f3de9`; version/output metadata only changed |

The return installation is in progress at this report's checkpoint. Its completed
readback, postboot and normal-operation results have not yet been recorded here.
Do not treat the prepared artifact or approval as a completed restoration.

Development source remains in [draft PR32](https://github.com/rtariq2015/cloud-stick-pro/pull/32)
and [draft PR33](https://github.com/rtariq2015/cloud-stick-pro/pull/33), unmerged.
The latest heads `167c603` and `aefd1dd` had queued CI runs at this checkpoint;
earlier passes at `69a3a99` and `ce69da4` are evidence for those exact earlier heads.
No normal update-feed entry, immutable release asset or private package has been
published by this test. The [credits and source/binary gate](../CREDITS.md) remain.

## Next decision

Keep Guest-Net optional. Full HTTP204 and certificate-verified HTTPS200, including
a usable clock, still need confirmation. If those succeed, propose adding an open
Guest-Net profile through the existing authenticated Check → Save flow, keeping
priority, enabled/delete controls and the eight-profile limit. This report does
not implement that save or enable automatic uploads on the guest network.

See the [test guide](../GUEST_WIFI_TEST.md) for commands, expected output and the
protected enable/disable process. The earlier
[development report](2026-11-11-guest-internet-diagnostic.md) retains its software
test and build evidence from before this physical test.
