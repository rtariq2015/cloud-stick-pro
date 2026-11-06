# Desktop frontend adapter

The `shared/` directory contains the five phone setup files copied byte-for-byte
from the frozen RC5 source. The desktop entry point is `index.html`, not the
unused `shared/index.html`. `app.mjs` is the desktop UI adapter: native Bluetooth,
secure-context/cancellation hooks, desktop support text and sanitized wizard
state publication. Shared Security2/connection bytes remain unchanged. Tests
compare all five shared files exactly, retain the secured client-operation
sequence, and execute the actual adapter against synthetic protocol clients.

`bootstrap.mjs` waits for actual secure-context/WebCrypto availability and the
native QWebChannel before enabling login. The application serves Qt's trusted
`qwebchannel.js` resource at the same-origin `qwebchannel.js` path. CSP remains
`script-src 'self'` and `connect-src 'none'`. The backend must permit only bundled
assets and prevent arbitrary navigation from receiving this bridge.

`bridge.mjs` allows only discovery, exact-device connection, four reviewed GATT
characteristics, disconnect, bounded USB status/setup assistance and read-only
bundle review. It never overrides `navigator.bluetooth`, stores credentials or
logs transport frames. Each native request has a monotonically increasing
positive 31-bit ID, a timeout and an independent pending record. A native
disconnect rejects pending GATT work. The native backend remains responsible for
serializing hardware operations and draining a preceding disconnect before a new
connection.

The accessible modal chooser requires an explicit selection. Teardown hides
private facts, clears the existing shared login/session state, cancels selection
and disconnects the link; late results cannot reauthenticate the interface.

The toolbar honestly separates USB diagnostics from Bluetooth settings. It does
not invoke USB controls during a Bluetooth operation, change firmware, implement
an application updater or claim that bundle metadata establishes compatibility.
First-owner creation still needs the current firmware's physical enrollment
procedure; authenticated login rotation is available through the existing shared
form.

The desktop wizard shows welcome, USB, sign-in, personal login, Wi-Fi, cloud and
finish one at a time. `wizard-model.mjs` holds only navigation/boolean facts;
`wizard.mjs` presents Back/Next and delegates explicit actions to the original
validated handlers. Next is not an implicit save. Staged edits block leaving;
unknown save outcomes return to authentication. A saved login requires a newer
authenticated connection before Wi-Fi. Recorded backup status is a separate view.
No password/key/SSID is stored in the model or browser storage. Read-only bundle
review stays under Advanced. `onboarding.mjs` supplies fixed USB help text.

USB failures use a fixed error-code-to-help table: Linux access permissions,
known/uncertain USB ownership, timeouts, changed devices and unsupported replies
have separate plain-language next steps. Raw exception text is never displayed.
Unknown ownership remains blocked, and retry always needs another user action.

Run the synthetic transport tests from the repository root:

```text
node --test desktop/tests/frontend-*.test.mjs
```

These tests cover the real frontend adapter with fake native signals and public
synthetic values. They do not demonstrate an actual Qt origin, Windows/Linux
Bluetooth adapter, USB device, authenticated hardware session or cloud service.
The parent application's Qt smoke test must verify the local secure origin,
WebCrypto, resource allowlist, bridge initialization and rendered controls.
