"""Source-only release regressions; no serial/device/cloud access or uploads."""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/wifi-integration'
DEPS = Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV = dict(os.environ, PLATFORMIO_CORE_DIR='/config/.unmanic/research/cloud-stick-platformio',
    PATH=str(DEPS / 'lib/llvm-18/bin') + os.pathsep + os.environ['PATH'],
    C_INCLUDE_PATH=str(DEPS / 'include'), LIBRARY_PATH=str(DEPS / 'lib/x86_64-linux-gnu'),
    LD_LIBRARY_PATH=str(DEPS / 'lib/x86_64-linux-gnu'),
    ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
    UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
records = []

def run(name, command, expected_stderr=''):
    result = subprocess.run(command, cwd=ROOT, env=ENV, capture_output=True,
                            text=True, timeout=300)
    (OUT / (name + '.log')).write_text(result.stdout + result.stderr)
    passed = result.returncode == 0 and result.stderr == expected_stderr
    records.append(dict(name=name, command=command, exit=result.returncode,
        passed=passed, stderr=result.stderr, expected_stderr=expected_stderr))
    (OUT / 'release-regressions.json').write_text(json.dumps(dict(
        passed=all(r['passed'] for r in records), desktop_only=True,
        physical_device_access=False, records=records), indent=2) + '\n')
    print('PASS' if passed else 'FAIL', name, flush=True)
    if not passed:
        raise SystemExit('Inspect ' + str(OUT / (name + '.log')))

run('wifi-phone-sdk', ['node', '--test', 'test/wifi_integration/connection-sdk.test.mjs'])
run('wifi-phone-ui', ['node', '--test', 'test/wifi_integration/phone-ui.test.mjs'])
run('owner-card', ['python3', 'test/wifi_integration/owner_card.test.py'])
run('phone-https-package', ['python3', 'test/wifi_integration/https_package.test.py'])
run('target-link-audit', ['python3', 'tools/audit_wifi_firmware.py'])
run('wifi-candidate-regression', ['python3', 'tools/test_wifi_candidate.py'])
# These exact two loopback curl failures are intentional negative WebDAV tests.
# Every other stderr byte, including any sanitizer diagnostic, fails the gate.
run('production-core', ['bash', 'test/run.sh'], 'curl: (52) Empty reply from server\n' * 2)
run('ble-policy', ['bash', 'test/run_ble_native.sh'])
run('m1-isolation', ['python3', 'test/milestone1_corrections/run_isolation_tests.py'])
run('ble-platform-prepare', ['python3', 'tools/test_ble_platform_prepare.py'])
run('sdk-lifecycle', ['python3', 'test/milestone1/test_sdk_lifecycle.py'])
run('m1-adapter-build', ['python3', 'test/milestone1_corrections/build_adapter_tests.py'])
run('m1-adapter', ['python3', 'test/milestone1_corrections/run_adapter_tests.py'])
run('sdk-security-build', ['python3', 'test/milestone1/build_security2_host.py', '--sanitize'])
run('sdk-rng-build', ['python3', 'test/milestone1/build_security2_host.py', '--sanitize', '--rng-lifetime'])
run('sdk-negative-protocol', ['python3', 'test/milestone1/test_security2_host.py'])
run('m1-phone', ['node', '--test', *[str(p.relative_to(ROOT)) for p in sorted(
    (ROOT / 'phone/milestone1/test').glob('*.test.mjs'))]])
print('PASS source release regressions; hardware acceptance remains pending', flush=True)
