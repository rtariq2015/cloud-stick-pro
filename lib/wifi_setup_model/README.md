# Wi-Fi candidate policy: source prototype

This module is not included by either firmware build. It is a bounded,
single-worker policy for the next phone setup milestone. It has no persistence,
radio, transport, reset, SD or cloud dependency. `commitInMemory` explicitly
means volatile model state, not durable save or an applied Wi-Fi connection.

`Candidate` keeps up to eight active profiles and a separate candidate copy.
IDs are not reused during its lifetime. Redacted snapshots expose SSID, explicit
open/WPA2 security, priority, enabled state and secret presence, never passwords.
SSID and passphrase limits count UTF-8 **bytes**, not display characters. New
validation retains the existing 32-byte SSID/8–63-byte passphrase limits and
additionally rejects malformed UTF-8. Enterprise, WEP, raw PSK and WPA3-only
profiles are not supported here.

The intended sequence is:

1. The future trusted session adapter establishes device-specific ownership and
   an open physical window. It constructs `Access`; these fields must never come
   from phone JSON, the fixed web password or the public synthetic M1 proof.
2. A sole owner worker calls `begin` with the last acknowledged active revision.
   Stage edits with explicit Keep/Replace/Forget. Keep requires identical SSID
   and security; open networks require an explicit empty-secret selection.
3. After the real backup/network lifecycle coordinator confirms quiescence,
   `startCheck` issues a session/transaction/edit/profile/attempt ticket. The
   internal worker can borrow that exact profile for a bounded service call.
   Borrowed views cannot be retained, logged, serialized or used reentrantly.
4. A worker reports the exact ticket's result. The phone must never supply the
   result or claim that accepting a portal on the phone validated the stick.
   Success requires the future approved endpoint probe in addition to Wi-Fi,
   DHCP and DNS. Portal/association alone does not enable commit.
5. `commitInMemory` requires successful validation of the unchanged candidate,
   the expected active revision and quiescent workers. Every failure preserves
   the old active model. A later persistent implementation must add its own
   verified two-generation save before acknowledging durable completion.

Edits invalidate outstanding checks. Repeated checks get different attempt IDs,
so a late previous success cannot complete a new attempt. Disconnect/revocation
must call `closeSession` from the trusted owner task. A terminal Security 2
rejection remains latched by that transport; the model is not an authentication
implementation and must not be used to clear that latch. Poll the fixed two-minute
candidate deadline with the monotonic device clock. Epoch wrap is handled; a
future timestamp is not interpreted as unsigned elapsed time.

All methods require serialization by that owner worker. Callbacks must enqueue
bounded completions and never call this model concurrently. `Access` and tickets
are internal capability context, not a new encryption or wire protocol. The
current prototype does not add such an adapter, provisioning endpoint, parser,
fragmentation scheme, enrollment, schema migration or failover manager.

Secret arrays are overwritten on replacement, removal, abort, expiry, session
close and object destruction using volatile byte stores. This is limited to the
model's own buffers. It cannot erase caller inputs, future driver copies,
compiler temporaries, flash history or browser process memory. It makes no
encrypted-at-rest or cryptographic-erasure claim.

Run `python3 tools/test_wifi_candidate.py` through the project's source-only
compose wrapper. Tests use public synthetic fixtures only. See the root
`WIFI_SETUP_SOURCE_PROTOTYPE.md` for measured sizes, build identity and remaining
integration gates.
