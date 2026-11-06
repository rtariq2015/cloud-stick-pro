#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out="${CSP_WORKER_TEST_OUTPUT:-/tmp/csp-backup-worker-tests}"
mkdir -p "$out"
g++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation \
  -g -O1 -fno-omit-frame-pointer -pthread -fsanitize=address,undefined \
  -fno-sanitize-recover=all -DCSP_ENABLE_BACKUP_SETUP=1 \
  -DCSP_ENABLE_WIFI_SETUP=1 -DCSP_ENABLE_BLE_MANAGEMENT=1 \
  -Itest/backup_integration/stubs -Iinclude \
  src/sync_engine.cpp test/backup_integration/worker.test.cpp -o "$out/worker-tests"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$out/worker-tests" >"$out/worker-tests.log" 2>&1
for scenario in fs-preexisting fs-partial fs-end-fail fs-begin-fail; do
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$out/worker-tests" "$scenario" >>"$out/worker-tests.log" 2>&1
done
cat "$out/worker-tests.log"
if grep -Eiq 'AddressSanitizer|LeakSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$out/worker-tests.log"; then
  echo 'FAIL sanitizer diagnostic in worker test log' >&2
  exit 1
fi
