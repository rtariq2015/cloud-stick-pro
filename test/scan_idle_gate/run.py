#!/usr/bin/env python3
"""Source-only native tests; synthetic clock/SD/RTOS/cloud, no device access."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "artifacts/scan-idle-gate"
DEPS = Path("/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr")
ENV = dict(os.environ, LD_LIBRARY_PATH=str(DEPS / "lib/x86_64-linux-gnu"),
           ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1",
           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / name for name in ["src/sync_engine.cpp", "src/pro_core.cpp", "include/pro_core.h", "include/pro_journal.h",
        "include/scan_idle_gate.h", "include/ble_feature.h", "include/backup_worker.h",
        "src/usb_storage.cpp", "include/pro_snapshot.h", "src/pro_journal.cpp",
        "src/webdav_client.cpp", "src/remote_health_worker.cpp", "src/remote_health_model.cpp"]]
    sources += [p for p in (ROOT / "test/scan_idle_gate").rglob("*") if p.is_file()]
    def hashes():
        return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(sources)}
    before = hashes()
    records = []
    def run(name, command):
        args = list(map(str, command))
        result = subprocess.run(args, cwd=ROOT, env=ENV, capture_output=True,
                                text=True, timeout=120)
        output = result.stdout + result.stderr
        (OUT / (name + ".log")).write_text(output)
        diagnostic = bool(re.search(r"AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:", output))
        passed = result.returncode == 0 and not result.stderr and not diagnostic
        records.append(dict(name=name, passed=passed, exit=result.returncode,
                            command=args, stderr_empty=not result.stderr,
                            sanitizer_diagnostic=diagnostic))
        print(name, "PASS" if passed else "FAIL", flush=True)
        if not passed:
            raise RuntimeError(output[-12000:])
    compiler = DEPS / "lib/llvm-18/bin/clang++"
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-Wno-misleading-indentation", "-Wno-unused-function", "-pthread",
             "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
             "-fno-sanitize-recover=all", "-Iinclude"]
    run("model-build", [compiler, *flags, "test/scan_idle_gate/model.test.cpp", "-o", OUT / "model"])
    run("model", [OUT / "model"])
    run("journal-inventory-build", [compiler, *flags, "test/scan_idle_gate/journal_inventory.test.cpp",
                                  "src/pro_core.cpp", "src/pro_journal.cpp", "-o", OUT / "journal-inventory"])
    with tempfile.TemporaryDirectory(prefix="csp-scan-inventory-") as temporary:
        run("journal-inventory", [OUT / "journal-inventory", Path(temporary) / "synthetic-receipts"])
    worker_flags = ["-DCSP_ENABLE_BACKUP_SETUP=1", "-DCSP_ENABLE_WIFI_SETUP=1",
                    "-DCSP_ENABLE_OWNER_CLOUD_SETUP=1",
                    "-DCSP_ENABLE_BLE_MANAGEMENT=1", "-DCSP_ENABLE_REMOTE_HEALTH=1",
                    "-Itest/scan_idle_gate/stubs"]
    # Stubs must precede the real include directory for hardware interfaces;
    # the harness includes the actual sync_engine.cpp and links actual Backup.
    common = [compiler, *worker_flags, *flags,
              "test/scan_idle_gate/worker.test.cpp", "src/pro_core.cpp",
              "src/remote_health_worker.cpp", "src/remote_health_model.cpp",
              "-Wl,--wrap=opendir", "-Wl,--wrap=readdir", "-Wl,--wrap=closedir", "-Wl,--wrap=stat"]
    run("worker-enabled-build", [*common, "-DCSP_ENABLE_SCAN_IDLE_GATE=1", "-o", OUT / "worker"])
    for scenario in ["scope-fields", "inventory-global", "foreign-verified", "outside-pending", "outside-transferred",
                     "local-pending-record", "local-transferred-record", "corrupt-inventory", "foreign-restart-change",
                     "idle", "stale-errno", "same-size", "pending", "corrupt", "error", "interleaved", "cancel",
                     "directory-error", "close-error",
                     "restart", "cloud-invalidate", "scope", "metadata", "metadata-race", "status-clear", "audit", "file-limit", "entry-limit"]:
        run("worker-" + scenario, [OUT / "worker", scenario])
    run("worker-default-build", [*common, "-o", OUT / "worker-default"])
    run("worker-default", [OUT / "worker-default", "default-off"])
    assert before == hashes(), "Sources changed during native gate"
    report = dict(schema="cloud-stick-scan-idle-source-tests/v1", passed=True,
                  desktop_only=True, device_contact=False, cloud_contact=False,
                  real_sd_operations=False, source_stable=True,
                  sanitizers=["ASan", "UBSan", "LeakSanitizer"],
                  source_sha256=before, records=records,
                  actual_components=["sync_engine worker", "Backup core", "idle model", "health reporter/model"],
                  synthetic_components=["RTOS", "USB generation", "SD directory/file bytes", "receipt I/O", "DAV/TLS", "clock"],
                  not_tested=["target compilation", "physical board/media", "real cloud", "real target heap/stack/timing"])
    (OUT / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS source-only no-change scan gate", flush=True)


if __name__ == "__main__":
    main()
