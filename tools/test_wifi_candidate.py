"""Desktop and pinned Xtensa compile gates. No device/credentials/network access."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/wifi-candidate'
out.mkdir(parents=True, exist_ok=True)
def run(label, command, env=None, timeout=120):
    result = subprocess.run(command, cwd=root, capture_output=True, text=True,
                            env=env, timeout=timeout)
    (out / (label + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode or result.stderr:
        raise RuntimeError(f'{label} failed ({result.returncode}): {result.stderr}')
    return result.stdout

environment = dict(os.environ,
    ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
    UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
common = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
          '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
          '-Iinclude', '-Ilib/wifi_setup_model', 'test/wifi_setup_model.test.cpp',
          'lib/wifi_setup_model/wifi_setup_model.cpp', 'src/pro_core.cpp']
run('native-build', ['g++', *common, '-o', str(out / 'wifi-model-tests')])
native = run('native-tests', [str(out / 'wifi-model-tests')], environment)
# No device runtime integration yet; compile the actual policy with the pinned
# target compiler to catch ESP32 ABI/compiler issues as well as desktop UB.
core = Path(os.environ['PLATFORMIO_CORE_DIR'])
tool = core / 'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++'
run('xtensa-compile', [str(tool), '-std=gnu++17', '-Os', '-Wall', '-Wextra',
    '-Werror', '-fno-exceptions', '-fno-rtti', '-ffunction-sections',
    '-fdata-sections', '-fstack-usage', '-Ilib/wifi_setup_model', '-c',
    'lib/wifi_setup_model/wifi_setup_model.cpp', '-o', str(out / 'wifi_setup_model.o')])
target_sizes = run('xtensa-size', [str(tool.with_name('xtensa-esp32s3-elf-size')),
                                 str(out / 'wifi_setup_model.o')])
target_undefined = run('xtensa-undefined', [str(tool.with_name('xtensa-esp32s3-elf-nm')),
                                         '-u', str(out / 'wifi_setup_model.o')])
assert not any(name in target_undefined for name in ['malloc', 'calloc', 'realloc', '_Zn', 'esp_', 'nvs_', 'WiFi', 'reset'])
probe = out / 'abi-probe.cpp'
probe.write_text('#include "wifi_setup_model.h"\n'
    'char csp_candidate_bytes[sizeof(pro::wifi_setup::Candidate)];\n'
    'char csp_snapshot_bytes[sizeof(pro::wifi_setup::Snapshot)];\n')
run('xtensa-abi-build', [str(tool), '-std=gnu++17', '-Ilib/wifi_setup_model',
                        '-c', str(probe), '-o', str(out / 'abi-probe.o')])
abi = run('xtensa-abi', [str(tool.with_name('xtensa-esp32s3-elf-nm')), '-S',
                        '--radix=d', str(out / 'abi-probe.o')])
phone = run('preview-tests', ['node', '--test', 'phone/wifi-preview/preview-service.test.mjs',
                             'phone/wifi-preview/app.test.mjs'])
sources = ['lib/wifi_setup_model/wifi_setup_model.h',
           'lib/wifi_setup_model/wifi_setup_model.cpp', 'test/wifi_setup_model.test.cpp',
           'tools/test_wifi_candidate.py']
sources += [str(p.relative_to(root)) for p in sorted((root / 'phone/wifi-preview').glob('*'))
            if p.is_file()]
report = {'pass': True, 'scope': 'desktop simulation + target object compilation only; no firmware integration',
          'native': native, 'xtensa_object_size': target_sizes, 'target_abi': abi,
          'undefined_symbols': target_undefined, 'phone': phone,
          'runtime_stderr_empty': True, 'sanitizers': environment['ASAN_OPTIONS'] + '; ' + environment['UBSAN_OPTIONS'],
          'source_sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in sources}}
(out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(native.strip())
print(target_sizes.strip())
print(abi.strip())
print('PASS strict sanitizers, target object compilation and fixture/UI lifecycle tests')
