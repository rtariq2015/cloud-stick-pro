# Combined optional-feature worker gate

Run `python3 test/combined_product_worker/run.py` through the workspace compose
wrapper. This compiles the actual merged `sync_engine.cpp`, health observer and
model, file-history cache and numeric event log together with all three optional
features enabled. The runner treats sanitizer output, nonempty stderr and every
nonzero exit as failures; ASan, UBSan and leak detection are fatal.

Six independent processes cover ordinary retirement, a held SD read view,
retirement and maintenance during an in-flight health request, a held connection
check TLS request, and a health HTTP failure. Local synthetic adapters own the
RTOS, SD view, receipt enumeration and DAV/TLS transport. `opendir` is intercepted
and never enumerates host or physical SD files. Numeric event-log I/O uses only
an automatically removed temporary directory.

The checks require TLS/SD/engine/remote destruction and event descriptor closure
before actual history capture, a verified metadata unmount before task reap,
cache erasure before the next worker's remote/TLS objects, and no worker health
request while a stopped worker is reserved for BLE. The actual cache erasure is
inspected byte for byte before free. A separate explicit cache release proves
the zero-cache precondition needed by the portal-probe contract.

The complete BLE/public-probe coordinator is deliberately not linked here; its
existing runtime adapter gate supplies that admission coverage. This additional
worker gate is desktop evidence, with a mocked TLS lifetime rather than the
production DAV publisher. It does not prove physical USB timing, real Bluetooth,
provider behavior, target heap/stack margin or any venue acceptance.
