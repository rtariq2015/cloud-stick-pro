# Phone protocol provenance and limits

`security2.mjs` is byte-identical to `phone/milestone1/security2.mjs`, the existing
port interoperating with the pinned official Espressif Security 2 patch1
SRP-6a/SHA-512/3072-bit and AES-GCM protocol. Its retained comments describe the
original synthetic prototype; they are not a claim that M2 uses public credentials.
M2 supplies `csp-owner` and the fixed private card key and refuses synthetic M1
metadata/proof. There is no new cipher, plaintext configuration fallback or
Security0/1 downgrade. SDK provenance and byte-safe counter/RNG corrections stay
in `sdk/pinned_ble/`.

`connection.mjs` derives from the reviewed M1 generation-bound lifecycle, adds
M2 capability validation, the encrypted csp-wifi v2 command mailbox and bounded
two-entry pagination. `app.mjs` provides the actual setup workflow; style/layout
derive from the local synthetic preview. No third-party scripts/CDNs or analytics
are loaded. The package checker verifies Security2 byte identity.

JavaScript BigInt SRP calculations are not constant-time; credential strings,
browser internals and driver copies cannot be guaranteed erased. Fields/buffers
are cleared as practical, but this is not hardware-backed secret storage or a
side-channel assurance. A production release needs cryptographic/threat-model
review and potentially a hardened official native client. Do not treat desktop
test success or HTTPS hosting as that review.
