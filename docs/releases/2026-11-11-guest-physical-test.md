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
physical appearance was not visually accepted in the 0.4.13 test. Authenticated BLE
recovery, dashboard restoration, cancellation under a live request and endurance
also remain separate acceptance work. No fix is claimed for the older
[BLE cleanup issue](https://github.com/rtariq2015/cloud-stick-pro/issues/5) or
[cloud readback issue](https://github.com/rtariq2015/cloud-stick-pro/issues/24).

## Normal return — completed

The owner separately approved returning to normal operation after testing. The
installer's downgrade protection requires a normal successor newer than 0.4.13;
the prepared return image is `0.4.14-return-dev`:

| Item | Verified return identity |
| --- | --- |
| Source | `aefd1dd1a1f9d7e3db5ca2b9b410cbab061a1511` |
| Application size | 1,704,752 bytes |
| Application SHA-256 | `26a14d0fa7dc42c95379423d93dd42d49da8cf959d82d530a4e6b2b5e7ef5b71` |
| Runtime | Same code and build flags as the previously installed source `c61791697a81e04a3738533b05dcd70e475f3de9`; version/output metadata only changed |

The approved return installation completed through the unchanged protected
installer. It made another fresh matching pair of encrypted 16 MiB recovery reads,
reopened them before the application-only write, and passed full flash preservation
and readback. The actual worker again had swap limit/current usage zero and core
dumps disabled. Postboot confirmed the expected firmware, existing owner and saved
Wi-Fi revision. No second installation was attempted after an uncertain write.

Independent USB diagnostics after the installer finished confirmed:

- Owner ready, no store fault and BLE off.
- Wi-Fi locally connected, backups unpaused and the normal worker running without
  a worker error.
- Dashboard listener active with no reported dashboard fault.

The normal worker initially waited for network time (`-2303`). A later independent
health snapshot cleared that error and reported one verified, zero pending and
zero failed, with the worker still running and unpaused. Those status counters are
not a new upload acceptance test; no freshly generated cloud upload is claimed.
Cold-boot RAM counters at zero did not establish that files or receipts were erased;
the protected readback preserved the internal data, and the separate generated-file
check above has its own scope. The later normal-network status does not change the
earlier Guest-Net internet verdict.

A laptop HTTP request to the normal dashboard timed out. A subsequent route check
found that the laptop was on Guest-Net while the stick's current address was on a
different network. The listener's reported healthy state does not turn that timeout
into a successful HTTP test. Browser reachability needs a shared, permitted network
path and remains unverified from this laptop.

Development source remains in [draft PR32](https://github.com/rtariq2015/cloud-stick-pro/pull/32)
and [draft PR33](https://github.com/rtariq2015/cloud-stick-pro/pull/33), unmerged.
The physical-report milestone `258771d` passed all six relevant PR32 checks.
PR33 at `aefd1dd` still had no completed reported checks at the last poll; do not
treat its queued run as a pass. Earlier passes at `69a3a99` and `ce69da4` retain
their own exact-head scope.
No normal update-feed entry, immutable release asset or private package has been
published by this test. The [credits and source/binary gate](../CREDITS.md) remain.

## Normal 0.4.15 follow-up using the laptop

The owner later approved and installed `0.4.15-wifi-screen`, a separate update
that displays the connected network's name. Its source is
`e853bfd7fffaae1f57170fa9f8d8763c17d9ba33`; application SHA-256 is
`05082ad573cc562e374f80d654bee89854ba010572eb7d714d692b7a4156ea85`.
The protected installation used two fresh matching encrypted 16 MiB recovery
reads, then passed application-only writing and full flash preservation/readback,
including the existing owner and saved Wi-Fi revision checks. The actual worker
again had zero swap limit/current usage and disabled core dumps. The owner
reported that the current network name was readable on the real screen. That is
an owner-reported observation, not an automated optical test or a photograph.
See [PR34](https://github.com/rtariq2015/cloud-stick-pro/pull/34) and the separate
[screen guide](https://github.com/rtariq2015/cloud-stick-pro/blob/feat/lcd-wifi-name/docs/WIFI_SCREEN.md).

The laptop was then used for two separate checks:

| Check | Actual result and scope |
| --- | --- |
| Laptop on open Guest-Net at 2.4 GHz | Confirmed at 2412 MHz; DNS passed; fixed HTTP check returned 204 and Google HTTPS returned 200 with normal OS CA and hostname validation; no redirect or sign-in form was seen |
| Laptop network cleanup | Original 5180 MHz Guest-Net connection restored; the temporary memory-only profile was deleted; saved profile IDs and the original profile settings were unchanged |
| Normal stick through the native Linux authenticated client | Fresh wired identity matched; Security2 authentication passed; Guest-Net scanned as open/supported at RSSI -73 dBm; the RAM-only connection check passed in 14.393 seconds |

The stick's normal radio diagnostic reported all six association, AP-name,
security and DHCP proof flags (`63`). The backup worker stayed stopped throughout
this Guest-Net candidate test. No profile was saved, no portal handoff or cloud
probe ran, and no file or cloud upload occurred during the check. Aborting the
transaction confirmed that the original profiles and saved revision were
unchanged. Independent USB diagnostics after normal disconnect confirmed BLE
off, memory restored, Wi-Fi connected and backups unpaused, without a reset.

This is actual native Linux client/protocol evidence, **not acceptance of the
desktop application's graphical interface**. It confirms association and DHCP
in normal firmware as well as in the earlier diagnostic image. The laptop's
successful 2.4 GHz internet tests still do not prove internet, TLS or cloud access
from the stick. The earlier 0.4.13 HTTP200, invalid-clock and skipped-HTTPS result
is unchanged. No captive-portal cause, coexistence fix, or resolution of the
existing BLE cleanup and cloud readback issues is established.

Raw addresses, radio identities, authentication material and device logs remain
private. The RAM-only check above is separate from the later approved normal-use
trial below.

## Later approved saved-profile and normal-use trial

The owner explicitly approved saving Guest-Net alongside the existing networks
and allowing normal backups for this separate trial. The native Linux Security2
client saved open Guest-Net as the third profile, preserved the previous two
profiles and advanced the saved revision by one. It confirmed that Guest-Net was
selected and locally connected. Disconnect cleanup restored memory and left BLE
off, Wi-Fi connected, backups unpaused and no store fault, without a reset.

A subsequent 75-second read-only observation confirmed that Guest-Net remained
the active profile. The fixed serial `CHECK` request was accepted. The newly
recorded cloud operation was a folder check, not a file upload:

| New operation | Actual result |
| --- | --- |
| Trace identity | New request 23, after baseline request count 22; `FolderCheck` operation 10 |
| Duration | 98 ms |
| HTTP/result code | `-1`; no HTTP response obtained |
| Response body | Not read |
| TLS read observer | `0`; this does not establish whether a TCP or TLS connection failed |
| Internet/cloud verdict for the stick | **Not verified** |

The final status still showed three verified and six pending records with backup
error `-4`. Those were existing records and status, not a newly verified backup
or evidence that this trial reproduced a Guest-Net final-readback `-4` failure.
The new `-1` folder check is the relevant fresh result.

Separate laptop controls reached the cloud provider's public `app.koofr.net`
site with certificate-verified TLS and HTTP303, without account credentials.
Forced-IPv4 controls also obtained verified HTTPS303 from that public cloud site
and HTTPS200 from Google. Together with the laptop's 2.4 GHz HTTP204/HTTPS200
results, these show working laptop paths. They do not identify why the board's
cloud connection failed before an HTTP response, or prove a captive portal,
certificate problem, filtering policy or working cloud-account access.

At the end of this observation, Guest-Net remained saved and selected and normal
backups were unpaused as approved. No portal handoff, browser terms acceptance or
additional firmware installation was performed. No new backup acceptance or code
fix is claimed; the older BLE cleanup and cloud readback issues remain open.

## Next decision

Keep Guest-Net optional alongside the preserved fallback networks. Its saved
profile and local connection are confirmed; standalone HTTP204, verified HTTPS200
and a freshly verified cloud backup remain unproven. The next investigation is
the board's connection failure before an HTTP response. A saved profile, laptop
internet result or previous verified counter must not be treated as that proof.

See the [test guide](../GUEST_WIFI_TEST.md) for commands, expected output and the
protected enable/disable process. The earlier
[development report](2026-11-11-guest-internet-diagnostic.md) retains its software
test and build evidence from before this physical test.
