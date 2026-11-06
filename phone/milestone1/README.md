# Cloud Stick Pro Bluetooth phone prototype

This standalone, dependency-free website provides an actual Espressif Security 2
patch-1 client for the feature-flagged Milestone 1 firmware. It exposes device
identification and sanitized connection status only. The ordinary production
firmware does not advertise this service.

The prototype uses public **synthetic** ownership credentials, never real Wi-Fi or
Koofr credentials. It does not establish production ownership. Read
[the protocol provenance and limitations](PROVENANCE.md) before using it.

## Interface

1. After hardware testing is explicitly approved and the prototype is installed,
   hold BOOT for three seconds and release it to open the physical setup window.
2. Open this page over trusted HTTPS in Android Chrome. Tap **Find Cloud Stick**.
3. Choose the `CSP-M1-` device. The page checks its protocol version and authenticates
   with Security 2 before showing its read-only device information.
4. Refresh or disconnect. A phone session ends after two minutes; setup/backups
   follow the firmware lifecycle. Stay in the foreground while connecting.

Disconnect, hidden/frozen pages and page restoration clear the secure badge and
all device identity. Returning to the page always needs a new **Find Cloud Stick**
action and authentication. A refresh without a session explicitly asks you to
connect again. Metadata uses the official nonempty `---` write/read exchange;
reading a fresh NimBLE characteristic alone does not produce a response.

Nothing has been published. Local staging and loopback-only TLS tests do not
establish trust on a real phone. Do not bypass HTTPS certificate
warnings, enable browser security bypass flags or enter real credentials. There
is no app/PWA background service, Wi-Fi provisioning or cloud-settings form yet.

## Desktop validation

From the enclosing `tool.plugin-builder` workspace, with the development container
running:

```sh
./compose.sh exec python3 /config/.unmanic/plugins/cloud-stick-pro/phone/milestone1/test/reference_vectors.py
./compose.sh exec node --test /config/.unmanic/plugins/cloud-stick-pro/phone/milestone1/test/*.test.mjs
./compose.sh exec env ASAN_OPTIONS=detect_leaks=1 CSP_SECURITY2_HOST=/config/.unmanic/plugins/cloud-stick-pro/artifacts/milestone1/host-security2/security2-host-asan node --test /config/.unmanic/plugins/cloud-stick-pro/phone/milestone1/test/sdk-interop.test.mjs
./compose.sh exec env PLAYWRIGHT_BROWSERS_PATH=/config/.unmanic/research/cloud-stick-browsers /config/.unmanic/research/cloud-stick-tools/bin/python /config/.unmanic/plugins/cloud-stick-pro/phone/milestone1/test/browser_preview.py
```

Build the SDK host harness first with `test/milestone1/build_security2_host.py`
(and `--sanitize` for its sanitized variant). If it is absent, the SDK interop tests
are explicitly skipped; that is not a protocol pass. Chromium validation uses
Playwright plus its OS/browser dependencies. Test screenshots and results go to
`artifacts/milestone1/phone/`, including clearly labelled visual previews and a
desktop SDK interop image. No command above connects to or changes physical hardware.

## Runtime-only HTTPS preparation

The package tool copies exactly the five runtime files to a new versioned public
directory. Its hash manifest, provenance, license copy and NGINX configuration
stay outside the public root. The upstream license is also retained inside a
comment in the served `security2.mjs`. The generated configuration binds only
loopback, uses explicit file routes and no-store caching, and does not start or
install a server. Choose an approved HTTPS owner/origin and certificate before
any future deployment; keep HSTS scoped to that host.

```sh
./compose.sh exec python3 /config/.unmanic/plugins/cloud-stick-pro/tools/phone_https_package.py --output /config/.unmanic/plugins/cloud-stick-pro/artifacts/milestone1-corrections/phone-https-package-final --release m1-corrections-20261103-r2
./compose.sh exec env PLAYWRIGHT_BROWSERS_PATH=/config/.unmanic/research/cloud-stick-browsers /config/.unmanic/research/cloud-stick-tools/bin/python /config/.unmanic/plugins/cloud-stick-pro/tools/test_phone_https_package.py --browser
```

The second command needs NGINX, OpenSSL, NSS certutil and Playwright/Chromium. It
starts an ephemeral loopback server and trusts a one-day test CA only in a
disposable browser subprocess home. It does not change the actual user's or
system certificate stores. It never disables certificate verification. Results
are desktop HTTPS/source tests, not Android, radio or physical-device acceptance.

## Wire contract

Service: `021a9004-0382-4aea-bff4-6b3f1c5adfb4`.

| Endpoint | Characteristic UUID | Access |
| --- | --- | --- |
| `prov-session` | `021aff51-0382-4aea-bff4-6b3f1c5adfb4` | Official Security 2 handshake |
| `csp-info` | `021aff52-0382-4aea-bff4-6b3f1c5adfb4` | GCM-authenticated read-only request/reply |
| `prov-version` | `021aff53-0382-4aea-bff4-6b3f1c5adfb4` | Public version/capability metadata only |

`csp-info` requests are UTF-8 JSON, at most 96 bytes:
`{"v":1,"id":1,"op":"info"}`. IDs increase within the authenticated connection.
Replies must match the ID and the exact sanitized schema in `connection.mjs`.
Unexpected fields, malformed UTF-8/JSON, authentication failure or transport
timeout close the connection. No passwords, SSIDs, IPs, cloud URLs, file names or
file content are requested or rendered.
