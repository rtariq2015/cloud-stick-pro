# Upload speed: real test stopped by a connection error

The upload-speed work has **not passed a real speed comparison yet**. A small
test on the installed 0.4.8 firmware encountered cloud-transfer errors. The next
firmware and default update-feed promotion are on hold while this is investigated.
[Track the problem in issue #24](https://github.com/rtariq2015/cloud-stick-pro/issues/24).

## What happened

The desktop app signed in normally, read private history and closed Bluetooth.
Independent USB checks confirmed Wi-Fi, backup and memory recovery before the
trial. Three existing verified entries, no pending/failed entries and no store
fault were reported.

The trial created five disposable synthetic files totalling 1,180,333 bytes in
the watched folder. It did not change existing files or read their contents.
After the normal quiet period, the dashboard briefly showed an upload, then
became unavailable. Independent USB later reported `-4`, the pinned HTTP
client's "not connected" error. Wi-Fi stayed associated, Bluetooth was closed,
backups were unpaused and no store fault was reported. Normal retries still
reported the three earlier verified entries and five waiting/failed test files.
A later observation also recorded `-11`, a read timeout.

An ordinary saved-cloud connection check subsequently succeeded. That checks
login and folder access, not a complete file transfer. The exact failing request
and root cause remain unknown. Neither a good Wi-Fi association nor a successful
folder check is proof of a new verified cloud backup.

The failed measurement remains recorded as failed. Raw metadata, addresses,
account details and private paths stay out of GitHub. Temporary files created by
this session have a `Cloud-Stick-Speed-Test-` prefix and are retained for diagnosis;
only unchanged files created by the test may be cleaned up. Internal receipts,
personal files and settings are not erased to make the results look successful.

## What the candidate improves in computer tests

The separate, uninstalled **0.4.9-upload-dev** candidate avoids repeated cloud
folder-creation requests in the same backup worker. Failed transfers discard
these hints. It keeps upload, both full content readbacks, destination checks,
conditional publication and receipt checks.

The actual transfer and receipt code passed simulated HTTP/TLS/storage tests:
ten distinct files in a three-level folder required **54 requests instead of
90**. That is a request-count reduction in this fixture, **not a measured 40%
increase in real upload speed**, and it is not a fix for the observed error.
[Candidate and test details](https://github.com/rtariq2015/cloud-stick-pro/pull/23).

## Exact firmware identities

| Item | Identity |
| --- | --- |
| Installed/reported firmware | `0.4.8-discovery-dev` |
| Last protected-install source | `f25d8a5e30df7be7dafd5a5fe2ef736a5c8ac11b` |
| Last protected-install app SHA-256 | `929fe7a85a0426eaac5a044d0b21d47bd543765d09011700bb495542f121f329` |
| Uninstalled candidate source | `d1232ba595ba6e40410c2ceb13d9bd5dad1ad8d8` |
| Uninstalled candidate app SHA-256 | `3c067ded61a7bdb91ba2e2b724408636bd9ce7ddde5567d966f7d1619421cbf2` |

There was no fresh flash readback during the baseline and no candidate flash.
The candidate build checked all 6,509 frozen SDK entries before and after;
partitions and USB/storage source gates passed. Hardware acceptance remains
separate from that successful build.

## Companion preview and what remains

The [Setup 0.7.3 / Viewer 0.4.3 preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-guided-companion)
has nine publicly downloaded, size/hash-verified assets from app source
`369e2ef21b8053812e449a6bc5549be357b77ba8`. Windows and Linux native builds,
installer checks and software tests passed. Its release notes disclose this
upload failure. Publishing those files did not promote the normal update feeds
or flash a stick; [PR #22](https://github.com/rtariq2015/cloud-stick-pro/pull/22)
remains draft pending this investigation.

The verified 0.7.3 Linux user app was installed, preserving the previous program
and all settings/recovery data. With the real stick plugged in, automatic USB
checking and the deferred app-update check both completed without pressing the
manual check button. The app was then reopened normally with temporary local
debugging removed. This did not update firmware or establish new upload success.

The exact Android APK was installed without clearing data and its pulled hash
matched. A fresh phone UI test stopped at the phone lock screen before assertions
or authentication; its helper and temporary input were removed. The earlier
30 successful phone assertions belong to Viewer 0.4.2, not this APK. The older
Bluetooth memory-recovery [issue #5](https://github.com/rtariq2015/cloud-stick-pro/issues/5)
also remains open.

Next work is to identify the failing cloud request, preserve every existing
authentication/TLS/storage/recovery guard, and repeat real small-file and
large-file comparisons with complete verification. Accurate completed-upload
duration also needs device start/end timestamps; the rolling speed sample cannot
be used to reconstruct it.
