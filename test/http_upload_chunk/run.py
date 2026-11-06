#!/usr/bin/env python3
"""Actual pinned method defect repro and derivative under fatal ASan/UBSan."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/http-upload-chunk'
DEPS = Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG = DEPS / 'lib/llvm-18/bin/clang++'
ENV = dict(os.environ, LD_LIBRARY_PATH=str(DEPS / 'lib/x86_64-linux-gnu'),
           PATH=str(DEPS / 'lib/llvm-18/bin') + ':' + os.environ['PATH'],
           ASAN_SYMBOLIZER_PATH='/usr/bin/addr2line',
           ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:allow_addr2line=true',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
spec = importlib.util.spec_from_file_location('pinned_http_upload', ROOT / 'tools/pinned_http_upload.py')
pin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pin)

def main():
    result = pin.verify()
    result.update(passed=False, desktop_only=True, physical=False, cloud=False,
                  strict_asan_ubsan=True, leak_detection=True, records=[])
    OUT.mkdir(parents=True, exist_ok=True)
    paths = [ROOT / name for name in (
        'src/http_upload_chunk_trial.cpp', 'src/webdav_client.cpp',
        'src/pro_core.cpp', 'include/upload_chunk_trial.h',
        'include/upload_chunk_feature.h', 'tools/pinned_http_upload.py',
        'sdk/pinned_http_upload/HTTPClient.cpp.orig')]
    paths += sorted(p for p in (ROOT / 'test/http_upload_chunk').rglob('*') if p.is_file())
    before = {str(path.relative_to(ROOT)): pin.sha(path) for path in paths}
    baseline = pin.function((pin.BASE / 'HTTPClient.cpp.orig').read_text())
    derivative = pin.function((ROOT / 'src/http_upload_chunk_trial.cpp').read_text(),
                             pin.SIGNATURE.replace('HTTPClient::', 'UploadHTTPClient::'))
    for label, body, chunk, patched in [('baseline', baseline, 1460, 0), ('trial', derivative, 4096, 1),('trial-1460', derivative, 1460, 1)]:
        folder = OUT / label
        folder.mkdir(exist_ok=True)
        (folder / 'actual_stream_method.inc').write_text(body + '\n')
        binary = folder / 'stream-tests'
        commands = [('build', [str(CLANG), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                              '-O1', '-g', '-fsanitize=address,undefined',
                              '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                              f'-DCSP_TEST_CHUNK={chunk}', f'-DCSP_TEST_PATCHED={patched}', f'-DCSP_UPLOAD_TX_BYTES={chunk}',
                              '-I' + str(folder), 'test/http_upload_chunk/stream.test.cpp', '-o', str(binary)]),
                    ('cases', [str(binary)])]
        for name, command in commands:
            run = subprocess.run(command, cwd=ROOT, env=ENV, capture_output=True, text=True, timeout=180)
            output = run.stdout + run.stderr
            log = folder / (name + '.log')
            log.write_text(output)
            diag = bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:', output))
            passed = run.returncode == 0 and not run.stderr and not diag
            result['records'].append(dict(variant=label, name=name, command=command, exit=run.returncode,
                                         stderr_empty=not run.stderr, sanitizer_diagnostic=diag,
                                         passed=passed, log_sha256=pin.sha(log)))
            print(label, name, 'PASS' if passed else 'FAIL', flush=True)
            (OUT / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
            if not passed:
                raise RuntimeError(output)
        if label == 'baseline':
            run = subprocess.run([str(binary), '--available-race'], cwd=ROOT, env=ENV,
                                 capture_output=True, text=True, timeout=30)
            output = run.stdout + run.stderr
            log = folder / 'expected-defect-reproduction.log'
            log.write_text(output)
            # Expected failure is scoped to this exact unsafe baseline case.
            # All final trial and correctness runs reject any diagnostic.
            reproduced = run.returncode != 0 and 'ERROR: AddressSanitizer: negative-size-param' in output and 'HTTPClient::sendRequest' in output
            result['baseline_negative_available_defect_reproduced'] = reproduced
            result['baseline_reproduction_exit'] = run.returncode
            result['baseline_reproduction_log_sha256'] = pin.sha(log)
            print('baseline cancellation defect', 'REPRODUCED' if reproduced else 'NOT REPRODUCED', flush=True)
            if not reproduced:
                raise RuntimeError(output)
    feature_cases = [
        ('default-disabled', [], None),
        ('invalid-value', ['-DCSP_UPLOAD_THROUGHPUT_TRIAL=2', '-DCSP_ENABLE_BACKUP_SETUP=1',
                           '-DCSP_ENABLE_WIFI_SETUP=1', '-DCSP_ENABLE_BLE_MANAGEMENT=1',
                           '-DCSP_PRODUCT_RC_PROFILE=1'], 'CSP_UPLOAD_THROUGHPUT_TRIAL must be 0 or 1'),
        ('missing-product-profile', ['-DCSP_UPLOAD_THROUGHPUT_TRIAL=1', '-DCSP_ENABLE_BACKUP_SETUP=1',
                                     '-DCSP_ENABLE_WIFI_SETUP=1', '-DCSP_ENABLE_BLE_MANAGEMENT=1'],
                                    'HTTP upload trial requires the explicit ordinary-folder product RC build'),
        ('missing-backup-runtime', ['-DCSP_UPLOAD_THROUGHPUT_TRIAL=1', '-DCSP_PRODUCT_RC_PROFILE=1',
                                   '-DCSP_ENABLE_WIFI_SETUP=1', '-DCSP_ENABLE_BLE_MANAGEMENT=1'],
                                  'HTTP upload trial requires the explicit ordinary-folder product RC build'),
        ('missing-distinct-image', ['-DCSP_UPLOAD_THROUGHPUT_TRIAL=1','-DCSP_PRODUCT_RC_PROFILE=1',
                                   '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1'],
                                   'HTTP upload trial requires its distinct speed trial image identity'),
        ('invalid-chunk', ['-DCSP_UPLOAD_TX_BYTES=0'],'CSP_UPLOAD_TX_BYTES must select a reviewed 1460 or 4096 byte buffer'),
    ]
    for name, flags, rejection in feature_cases:
        command = [str(CLANG), '-std=c++17', '-E', '-P', '-x', 'c++',
                   '-Iinclude', '-Itest/http_upload_chunk/stubs',
                   '-Itest/backup_integration/dav_stubs', *flags, '-']
        run = subprocess.run(command, input='#include "ble_feature.h"\n#include "upload_chunk_trial.h"\n',
                             cwd=ROOT, env=ENV, capture_output=True, text=True, timeout=30)
        output = run.stdout + run.stderr
        passed = (run.returncode != 0 and rejection in run.stderr) if rejection else (
            run.returncode == 0 and not run.stderr and 'using UploadHTTPClient = HTTPClient;' in run.stdout)
        log = OUT / (name + '.log')
        log.write_text(output)
        result['records'].append(dict(variant='feature-policy', name=name, command=command,
                                     exit=run.returncode, expected_rejection=rejection,
                                     passed=passed, log_sha256=pin.sha(log)))
        print('feature', name, 'PASS' if passed else 'FAIL', flush=True)
        if not passed:
            raise RuntimeError(output)
    for enabled,chunk in [(0,1460),(1,4096),(1,1460)]:
        suffix=f'{enabled}-{chunk}'
        binary = OUT / f'adapter-{suffix}'
        compile = [str(CLANG), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-Wno-misleading-indentation', '-O1', '-g',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-fno-omit-frame-pointer', '-DCSP_ENABLE_BACKUP_SETUP=1',
                   '-DCSP_ENABLE_WIFI_SETUP=1', '-DCSP_ENABLE_BLE_MANAGEMENT=1',
                   '-DCSP_PRODUCT_RC_PROFILE=1', f'-DCSP_UPLOAD_THROUGHPUT_TRIAL={enabled}',
                   f'-DCSP_PRODUCT_SPEED_TRIAL={enabled}',
                   f'-DCSP_UPLOAD_TX_BYTES={chunk}',
                   '-Itest/http_upload_chunk/stubs', '-Itest/backup_integration/dav_stubs',
                   '-Iinclude', '-Ilib/tinyxml2/src', 'src/http_upload_chunk_trial.cpp',
                   'test/http_upload_chunk/adapter.test.cpp', 'src/pro_core.cpp',
                   'lib/tinyxml2/src/tinyxml2.cpp', '-lcrypto', '-Wl,--wrap=fopen',
                   '-o', str(binary)]
        for name, command in [('build', compile), ('cases', [str(binary)])]:
            run = subprocess.run(command, cwd=ROOT, env=ENV, capture_output=True, text=True, timeout=180)
            output = run.stdout + run.stderr
            log = OUT / f'adapter-{suffix}-{name}.log'
            log.write_text(output)
            diag = bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:', output))
            secrets = any(secret in output for secret in ('synthetic-user', 'synthetic-secret', 'synthetic-ca'))
            passed = run.returncode == 0 and not run.stderr and not diag and not secrets
            result['records'].append(dict(variant=f'actual-adapter-{suffix}', name=name,
                                         command=command, exit=run.returncode,
                                         stderr_empty=not run.stderr, sanitizer_diagnostic=diag,
                                         secrets_absent=not secrets, passed=passed,
                                         log_sha256=pin.sha(log)))
            print('adapter', enabled, name, 'PASS' if passed else 'FAIL', flush=True)
            (OUT / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
            if not passed:
                raise RuntimeError(output)
    after = {str(path.relative_to(ROOT)): pin.sha(path) for path in paths}
    result['source_stable'] = before == after
    result['source_sha256'] = after
    assert result['source_stable'], 'Source changed during the run'
    result['passed'] = True
    result['sanitizer_scope'] = 'Expected failing original-case repro only; every final derivative/correctness run has no diagnostics'
    (OUT / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS source/transport simulation only; no speed or hardware claim', flush=True)

if __name__ == '__main__':
    main()
