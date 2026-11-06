# Remote health model desktop gate

Run `python3 test/remote_health_model/run.py` through the existing source-container
wrapper. The test uses the real `remote_health_model.cpp`; there is no hardware,
SDK, filesystem, account or network integration in the model.

The flag defaults to zero locally. Default and explicit-disabled model objects
contain no model symbols, report strings or external references. Enabling the
model adds no resident report buffer and no BSS/data in the inspected host object.
Snapshot and Schedule are caller-owned state. Serialization performs no heap
allocation; a caller supplies at most 1024 bytes including the final NUL.

Strict ASan/UBSan/LeakSanitizer tests cover all output capacities, sentinel
boundaries, maximum permitted values/longest enum labels, firmware text
whitelisting, nonterminated strings, invalid enum/time/capacity data, known versus
unknown observations, independent exponential retry, cancellation without a new
acknowledgment, failure saturation and millisecond wrap. Unknown scan counters
are omitted; cached capacity and observation times remain explicitly historical.
Read-view inactivity is not presented as SD failure, and cloud read checks are
separate from the last receipt-backed file verification time.

Reports are private, human-readable snapshots. An old report or silence does not
prove power loss, and this model never claims that it published itself or that
all current files are protected. No filenames, network names/addresses,
destinations, account names, credentials or free error text are accepted.

Native stack `.su` and object-size evidence is captured beside the test logs.
Those are host-compiler measurements, not target stack high-water marks or ESP32
heap admission evidence. The caller's output buffer, Snapshot, and libc formatting
stack costs must be included in later actual-target stack measurements.
