# Lab-only numeric panic evidence

`CSP_PANIC_DIAGNOSTICS` defaults to zero and is enabled only by the explicit
`tdongle_s3_product_lab4` profile. This is instrumentation for the observed
repeated-session reset, not a correction or acceptance claim for that reset.

The pinned Arduino 3.3.12 core already defines a strong
`__wrap_esp_panic_handler` in `cores/esp32/esp32-hal-misc.c:463–469`.
Lab4 adds only the standard `--wrap=esp_panic_handler` link flag, activating that
existing wrapper, and registers `csp_lab_record_panic` through the existing
`set_arduino_panic_handler` API. No replacement wrapper, linker alias,
multiple-definition override, SDK edit or panic/reset/watchdog-policy change is
used. The original wrapper always continues to `__real_esp_panic_handler` after
the callback returns. Its exact body is extracted for the native call-chain test.

The callback ABI comes from the frozen SDK's `esp32-hal.h:180–194`:
core, reason pointer, PC, backtrace flags, count and 60 processed PC values.
The project never copies the reason pointer/text. The callback itself is
`IRAM_ATTR` and writes only numeric core/PC, corruption/truncation flags and at
most **16 processed PCs**. It performs no allocation, formatting, locking, serial
I/O, filesystem access or raw stack/register copy. A fixed 96-byte
`RTC_NOINIT_ATTR` record includes a version and checksum; the commit magic is
written last after a target `memw`. The checksum detects corruption, not forgery.

At early application setup the previous record is accepted only for
`ESP_RST_PANIC`, matching magic/version/checksum, core 0/1, bounded count and
boolean flags. Power-on, ordinary software-reset and watchdog-reset reasons do
not claim a valid captured panic. The accepted record is copied into normal RAM
for this boot; RTC magic is then cleared before registering the callback. Thus
a later panic whose official walker fails before the callback cannot reuse an
older record. The RTC area is not a persistent log and may be lost to power loss.

`PANIC_STATUS` is a read-only serial command with exactly these fields:
`panic_valid`, `reset_reason`, `core`, `pc`, `count`, `backtrace_corrupt`,
`backtrace_truncated`, `pcs`. It formats the boot-time RAM snapshot only, with a
384-byte stack buffer in a separate non-inlined handler. Invalid records return
no PCs and zero core/PC/count. It has no file names, addresses of data buffers,
SSID, account, reason string, key or raw memory. The default-off build retains
the ordinary unsupported-command response and has no diagnostic module symbols.
No USB initialization, worker admission, NVS, configuration, SD or receipt
operation is part of this command.

## Placement and limitations

The existing target map before this instrumentation placed
`esp_backtrace_get_next_frame` in IRAM at `0x40375d5c`; its source in
`esp_system/port/arch/xtensa/debug_helpers.c:27–36` uses the official stack/PC
sanity checks. The original Arduino wrapper and `handle_custom_backtrace` are
not explicitly IRAM-attributed and were discarded before the new link flag.
They must be located in the rebuilt exact ELF/map. The SDK's standard panic
entry normally re-enables cache before reaching them, but cache-off/damaged
stack/nested panic can prevent the walker or callback from completing. This
instrumentation does **not** claim coverage for those failures. The existing
walker still applies its original maximum 100 iterations/60 collected PCs;
the callback exports at most 16 and reports truncation. Corrupt traces remain
marked corrupt and are not proof of a complete call chain.

Before hardware use, independently prove `csp_lab_record_panic` is in IRAM,
`retained` is in `.rtc_noinit`, the module's normal snapshot is in RAM, and the
existing strong Arduino wrapper resolves to the original SDK handler. Retain
the exact ELF/map/hash for decoding PC values. No target build or physical panic
was performed by this lane.

## Desktop evidence

Run `python3 test/panic_diagnostics/run.py` in the existing workspace compose
container. It runs the actual recorder/formatter under fatal ASan/UBSan and leak
checks, using the exact pinned callback struct and wrapper body. The target
unwinder is a synthetic producer in this native test; it is not executed on x86.
Cases cover the 16-PC bound, 60/oversized counts, continued/corrupt traces,
invalid/null inputs, magic/version/checksum/count/flag rejection, partial commit,
reset reasons, stale-record clearing, idempotent setup and formatting bounds.
There are 22 recorder cases and 31 total command records, including compilation,
two expected compile rejections outside the allowed feature/profile policy,
the default-off symbol check and both actual UART variants.
Actual runtime UART tests exercise enabled/default-off dispatch and prove no
NVS/configuration/worker/radio/USB mutation. Target IRAM placement, retention
across a real panic, and decoding the repeated public-session failure remain
separate physical gates. Evidence is `artifacts/panic-diagnostics/results.json`.
