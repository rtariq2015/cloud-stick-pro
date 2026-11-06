# Espressif sources used only by the desktop Security 2 test

These files are copied unchanged from the ESP-IDF **v5.5.5** tag. `PROVENANCE.json`
records each official Git blob ID and source URL. Verify those blob hashes before
building. Original copyright/SPDX headers are retained; see `LICENSE`.

The server compiles the actual Security 2, SRP and generated protobuf C source.
Its public synthetic salt/verifier are extracted from `src/ble_management.cpp`
with pinned SHA-256 checks and generated into an artifact header; these match the
firmware's Milestone 1 fixture exactly. This does not test arbitrary salt encodings
or production ownership credentials.
`host_shims/` supplies Linux randomness/event/log stubs and aliases mbedTLS 3 API
names to equivalent mbedTLS 2.28 error-returning APIs. It does not implement new
cryptography. The server is not linked into firmware and cannot access a radio,
USB, NVS or the microSD. Sec0/Sec1 generated descriptors are needed by the session
protobuf type but no Security0/Security1 implementation is compiled or selected.

This tests desktop protocol interoperability, not BLE transport, ESP hardware
crypto, FreeRTOS behavior, the precompiled SDK binary, or physical device heap.
