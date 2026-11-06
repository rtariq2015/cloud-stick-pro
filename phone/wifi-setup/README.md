# Owner-authenticated phone setup source

Five runtime files, no build dependencies. Source-only development version
`0.3.3-wifi-m2-dev-phone.1`; requires the separate M2 Wi-Fi firmware and a fixed
private owner card. The old published M1 page/credentials cannot use it.

Prepare with `python3 tools/prepare_wifi_phone_package.py` from the project root.
Only `artifacts/wifi-integration/phone-https/public/` is the proposed hosting
root; never serve this repository/folder directly. Deployment is not authorized
or performed by that tool. See the root `MILESTONE2_PHONE_HTTPS_PACKAGE.md` for
headers, privacy, hashes and final-origin acceptance.

The localhost synthetic preview remains separate at `phone/wifi-preview/`.
This page intentionally refuses private key input on HTTP, including localhost.
Normal sign-in now accepts a saved username/password through the same official
Security2 client. Existing key/card login remains available under **Use my
existing private key** and still uses `csp-owner`. Nothing automatically replaces
the current owner record. On compatible firmware only, the new-login card creates
a canonical verifier locally, stages it and requires explicit confirmation before
durable replacement. The suggested `admin` username applies only to that new-login
form; it is not a factory credential.

See `docs/OWNER_LOGIN_PHONE_IMPLEMENTATION.md` for the new source contract,
cryptographic provenance, compatibility limit, tests and physical-test gates.
The runtime still contains exactly five files. The Security2 handshake/cipher
body is unchanged; a bounded COW2 generator reuses its pinned SRP primitives in
the same module. The existing package tool's historical release label must be
updated by final integration before deployment.

The optional owner cloud setup candidate uses the same authenticated Security2
mailbox. Its Koofr fields appear only when the device advertises `cloud:true`
and `cloud_protocol:1`; existing `cloud:false` firmware still supports Wi-Fi,
recorded backup status and history on this page. The normal firmware build keeps
owner cloud setup disabled. This source change does not install it or publish
the page.

Cloud changes are staged per field, reviewed, then durably committed only after
explicit consent. Saved login/folder values are write-only; reads return presence
and revision facts. No cloud test or upload is performed by this page. Finish
setup to disconnect Bluetooth, then read the later backup record separately.

See `docs/OWNER_CLOUD_PHONE_SETUP.md` for the source contract, local test commands
and limits. Native crypto interoperability and simulated DOM tests are not
Android/RF acceptance. The additional Chromium preview uses a synthetic service
and locally intercepted resources; it does not connect to a real device or cloud.
