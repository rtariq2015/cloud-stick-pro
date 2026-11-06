# Local Wi-Fi setup preview

An interactive **fixture preview**, separate from `phone/milestone1` and the
installed Bluetooth test. It demonstrates choosing a listed/hidden example
network, private/open distinctions, connection failures, Accept-page limitations,
explicit save and cancel. The example password is fixed and read-only. No real
credentials, Bluetooth, Wi-Fi, Koofr or browser storage are used.

`preview-service.mjs` is a UI fixture, **not** a second implementation of the C++
candidate policy or ownership. A future real adapter must invoke that policy
behind authenticated Security 2, and use durable/applied/tested revisions rather
than treating this demonstration's save as a device save.

On this Linux computer, the local preview runs at **http://127.0.0.1:8787/**.
It is not reachable from a phone by using the phone's own `127.0.0.1`. It is not
the published Bluetooth URL. The source-only helper runs under a distinct
container with a read-only mount of this folder, no devices, no capabilities,
no additional privileges, and a port published only to host loopback. The HTTP
handler serves exactly four files, denies all other paths and POST, and sets
no-store, JavaScript MIME types, strict CSP and denied Bluetooth permissions.
There is no credential collection in this HTTP preview. Real phone Bluetooth
setup still requires its separately reviewed trusted HTTPS origin and owner proof.

Start/stop from `/home/home/tool.plugin-builder`:

```sh
./compose.sh -f build/research/cloud-stick-ble-auth-cleanup/phone/wifi-preview/compose.preview.yml up -d csp-wifi-preview
./compose.sh -f build/research/cloud-stick-ble-auth-cleanup/phone/wifi-preview/compose.preview.yml stop csp-wifi-preview
```

Portable server entry point: `python3 phone/wifi-preview/serve.py`; use the
workspace compose wrapper to execute it here. Only map a loopback host port.
Do not serve the repository root or substitute a general directory server.
The compose profile defaults to this checkout's absolute preview directory:
the workspace wrapper resolves relative bind paths against the workspace root.
For a different checkout, set `CSP_WIFI_PREVIEW_SOURCE` to that checkout's
absolute `phone/wifi-preview` directory before launching.

Tests:

```sh
node --test phone/wifi-preview/preview-service.test.mjs phone/wifi-preview/app.test.mjs
python3 phone/wifi-preview/static_test.py
```

`browser_test.py` additionally exercises the actual page in sandboxed Chromium
and captures screenshots. **That check is currently blocked by the container's
unavailable Chromium sandbox.** Browser dependencies were extracted into a local
tool directory; no application dependency was added and sandboxing was not
disabled. No screenshots or visual/Android acceptance are claimed. The fourteen
passing Node tests run the actual app body with a simulated DOM plus its actual
fixture service; they do not prove rendering, accessibility or phone lifecycle.

The page's CSP denies outbound connections and no scripts load third-party
assets. Cancel, hidden/frozen pages and restoration clear drafts; late scan/check
results cannot revive them. Reload restores the initial example. A real settings
screen remains blocked until the owner, command, lifecycle and persistent-store
integration gates in the main report are met.
