# Private activity and local key-file phone tests

2026-11-04, `integration/public-wifi-assistance` worktree. **Source/desktop gate:
PASS, 397 tests**: 162 activity/TXT/native-runner cases, 113 public Wi-Fi
cases and 122 existing phone/UI/scan/backup regressions. All passed without
stderr, failures, skipped/cancelled tests or todo tests; source hashes remained
stable during the final run.

The new tests import the actual `ManagementConnection`, `DiagnosticLogClient`,
`WifiSetupClient`, `PublicWifiClient` and evaluate the actual phone app and
native runner bodies. DOM, GATT, Security 2 transport, stdin, process spawning
and bridge RPC are synthetic. No real private file, Bluetooth radio, BlueZ,
D-Bus, cloud account or published website is used. These tests do not establish
cryptographic interoperability, Android behavior or physical radio acceptance.

## Implemented phone behavior

The version metadata may advertise exactly `event_log: true` with
`event_log_protocol: 1`. Missing/false capability keeps logging unavailable;
invalid types or protocol versions are rejected. `Info.event_log: true` is a
local annotation made only after Security 2 authentication. The authenticated
wire Info schema is unchanged and cannot inject that capability. Only an
authenticated, capable `ManagementConnection` admits `event_log` commands.

`DiagnosticLogClient.read()` sends `{cursor: uint32}`, beginning at zero, and
reads at most twelve one-event pages. It requires the exact reviewed schema:

```text
{ok, events:[{s,u,t,k,c}], next, more, newest, persisted,
 loaded, dirty, corrupt, io_failed, exhausted}
```

All records/status fields are numeric or boolean. Sequence and uptime fit
uint32; time is zero or within 1767225600–253402300799; kind is 1–8; code is
int32, except VerifiedCount kind 4 uses 0–128. Fields, cursor progress, bounds,
duplicates, sequence gaps after the first retained event, empty-page loops and
changed snapshot metadata are checked. The entire paused-session snapshot,
including persistence flags, must stay constant. Invalid data closes setup
without reflecting unknown text. Existing 400-byte response budgeting is kept;
one maximal numeric page is tested against that budget.

The authenticated **Recent device activity** section shows recorded event
numbers, times/uptime, typed numeric outcomes and explicit storage caveats.
It states that partial scans/counts and connection checks do not prove every
file uploaded; backup receipts remain verification truth. Unloaded history,
corruption, I/O failure, unsaved RAM facts and sequence exhaustion are described
without changing receipt facts. Every teardown, page restoration, freeze,
hidden-page transition, remote disconnect and late response clears private
activity. Public AP instructions may remain after a completed Settings handoff;
activity and authentication do not.

The same local file input now accepts a TXT key or the existing JSON owner
card. A TXT key must be exactly 32 `[A-Za-z0-9_-]` characters after trimming,
with at most 256 actual UTF-8 bytes. The overall pre-read file limit remains
2048 bytes for JSON; an actual byte-length check also catches misreported size.
No filename is inspected or displayed. Text is not uploaded, placed in URLs,
stored in browser storage, or logged. Keys clear on connect/teardown and late
file reads cannot restore them. The strict JSON schema and authenticated device
identity binding remain intact. TXT imports clear any previous JSON binding;
the private Security 2 owner key remains required.

## Native runner modes

`tools/native_ble/run.mjs` keeps its existing stdin-only private parameters and
existing modes. Two modes were added:

- `activity`: after private authentication, read the strict numeric event pages
  and emit only the sanitized numeric/boolean log snapshot.
- `portal`: read saved profiles and a fresh selected-network status, require an
  exact durable/applied active profile, prepare and fully read the handoff ACK
  (including pending-result polling), then explicitly disconnect and await the
  bridge disconnect request. No scan, stage, check or save is performed.

Neither new mode prints AP name/password, grant, owner key, URL, private filename
or venue SSID. Free-form firmware text is omitted in these modes. The handoff
output says only that the ACK was read and disconnect requested; it does not
claim an AP started, venue terms were accepted, cloud access worked or files
uploaded. Tests execute the actual runner body with inert spawn/stdin/RPC and
the actual production client stack over synthetic GATT, and verify ordering,
redaction, refusal paths and preserved identity/scan modes.

## Reproduce and evidence

From the workspace root:

```bash
./compose.sh -f docker-compose.yml \
  -f build/research/cloud-stick-auth-cleanup-compose.yml \
  exec -T --user 1000:1000 \
  --workdir /home/home/tool.plugin-builder/build/research/cloud-stick-public-wifi-stage \
  unmanic-dev python3 test/event_log_phone/run.py
```

`artifacts/event-log-phone/results.json` records exact source/runner hashes,
commands, test totals and TAP evidence. Reported SHA-256:
`b5e7c23b95e1f132e74a0b12492dbc023aa56a02e8f422903abec0a03f2136d1`.

The 320/390/800-pixel Chromium layout runner was updated with a twelve-event
numeric fixture and retried with the sandbox required. Launch is still
**BLOCKED** by `No usable sandbox!` in this container/host. No protection was
weakened and no screenshot or rendered-layout pass is claimed. Evidence is in
`artifacts/public-wifi/phone/layout/results.json` and its sandbox launch log;
the latest attempt is also captured in `artifacts/event-log-phone/layout-attempt.log`.

Actual device/target build gates belong to the integration owner. This lane
performed no firmware/SDK build, device, cloud, website deployment or commit
operation. The backend export must remain authenticated, maintenance-gated and
worker-stopped; desktop client tests do not replace those C++/hardware checks.
