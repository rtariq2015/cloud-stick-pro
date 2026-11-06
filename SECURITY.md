# Security policy

Cloud Stick Pro is development software for the original LILYGO T-Dongle S3.
There is no declared production support window or completed clinical/workstation
certification. Each firmware/app candidate needs its own review and acceptance;
old test results do not certify a new image.

## Reporting a security problem

Use the repository's private vulnerability-reporting channel **if enabled**.
If no private channel is available, request a private contact method without
posting the vulnerability details publicly. No dedicated security email address
or response-time commitment is currently declared.

Provide the affected component/version or commit, a synthetic reproduction,
expected/actual behavior and impact. Include only the minimum sanitized evidence.
Do not send live passwords, owner cards, raw flash/NVS images, signing keys,
clinical files or unredacted private history. Do not test against networks,
accounts or devices you are not authorized to use.

## Boundaries the project relies on

- The computer writes the USB-visible microSD filesystem. Firmware backup readers
  are read-only and invalidated before host sector writes. A quiet interval is
  not proof that a host application flushed or closed a file.
- A successful upload acknowledgment is not a verified receipt. Verification
  checks cloud bytes, while publication/receipt persistence have their own gates.
  Historical verification does not guarantee future cloud retention.
- Bluetooth management uses Espressif Security 2 without a plaintext or weaker
  security fallback. Authentication failure rejects the session; uncertain radio
  cleanup retains the barrier against conflicting work.
- Desktop/Android bridges admit bounded typed operations from bundled local
  assets and bind work to the selected device and session. Cancellation and page
  lifecycle changes must invalidate late responses and private results.
- Account changes require explicit review/save. First-owner creation is restricted
  to a genuinely Missing record and the firmware's physical gates; it is not a
  takeover or forgotten-password bypass for an owned stick.
- HTTPS WebDAV uses certificate/host verification and an appropriate clock.
  Cloud credentials must not be forwarded to portal or redirected hosts.

The local HTTP dashboard is a **read-only status surface**, not a credential-entry
page. Other clients on a reachable local network may see its sanitized status;
do not expose it through router port forwarding. Private history belongs to the
authenticated interface. Nearby BLE access is not an Internet remote-management
service.

## Important remaining limits

Internal credentials are not protected by a completed secure-boot/flash-encryption
deployment, and removable files are unencrypted. Physical possession therefore
matters. Keep private recovery backups encrypted and access-controlled outside
the repository; do not distribute a credential-bearing device clone.

The shared JavaScript Security 2 implementation uses BigInt arithmetic that is
not constant-time. Managed strings, driver copies and browser internals cannot
be guaranteed erased. Existing protocol interoperability and negative tests are
not an independent cryptographic or side-channel certification; retained
provenance documents state these limits.

Captive-portal assistance is experimental and network-dependent. A portal login
on a different device is not proof of the dongle's access, and accepting terms
may need repeating after expiry. The project must not bypass portal terms, TLS
checks or organizational network policy to report success.

Generic factory provisioning and a real desktop firmware writer remain gated.
Manifest review alone does not establish board compatibility or perform an
update. Recovery must preserve current device identity, settings, receipts and
storage ownership, with reviewed image hashes and a fresh private backup.
Irreversible eFuse/security provisioning is a separate design and operation.

See [storage safety](docs/STORAGE_SAFETY.md),
[developer lifecycle requirements](docs/DEVELOPER_GUIDE.md#preserve-lifecycle-and-data-guarantees)
and [component provenance](docs/CREDITS.md) for the implementation context.
