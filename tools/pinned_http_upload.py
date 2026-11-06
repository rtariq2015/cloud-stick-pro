#!/usr/bin/env python3
"""Check a narrow, reproducible HTTP stream derivative; never edits the SDK."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'sdk/pinned_http_upload'
SDK = Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages/framework-arduinoespressif32')
CPP_SHA = '800cce94ff11c2acc53ed246d43a84d91e4997d144d460bf831cfe24deb9df01'
HEADER_SHA = 'f3a1e4ca8561338e23b3f277c2d26a41062aaf78d5f27ba0bb1bdee235b77efa'
SIGNATURE = 'int HTTPClient::sendRequest(const char *type, Stream *stream, size_t size)'
SYMBOL = '_ZN16UploadHTTPClient11sendRequestEPKcP6Streamj'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def function(text, signature=SIGNATURE):
    assert text.count(signature) == 1, 'Expected one exact pinned stream method'
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def derivative(original):
    changed = original.replace('int HTTPClient::sendRequest(', 'int UploadHTTPClient::sendRequest(', 1)
    old = 'int buff_size = HTTP_TCP_TX_BUFFER_SIZE;'
    assert changed.count(old) == 1
    changed = changed.replace(old, 'int buff_size = CSP_UPLOAD_TX_BYTES;', 1)
    old = '      int sizeAvailable = stream->available();\n\n      if (sizeAvailable) {'
    new = '''      int sizeAvailable = stream->available();

      // Cloud Stick: available() may change after the while admission check.
      // Reject terminal source invalidation before a negative int can become
      // an unbounded size_t passed to readBytes(). Zero retains the SDK yield.
      if (sizeAvailable < 0) {
        free(buff);
        return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
      }

      if (sizeAvailable) {'''
    assert changed.count(old) == 1
    changed = changed.replace(old, new, 1)
    old = '        int bytesRead = stream->readBytes(buff, readBytes);\n\n        // write it to Stream'
    new = '''        int bytesRead = stream->readBytes(buff, readBytes);
        // A revoked/read-failed source must not produce a successful payload.
        if (bytesRead <= 0 || bytesRead > readBytes) {
          free(buff);
          return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
        }

        // write it to Stream'''
    assert changed.count(old) == 1
    return changed.replace(old, new, 1)

def verify(sdk=SDK):
    assert sha(BASE / 'HTTPClient.cpp.orig') == CPP_SHA, 'Preserved HTTP source changed'
    original = function((BASE / 'HTTPClient.cpp.orig').read_text())
    wanted = derivative(original)
    actual = function((ROOT / 'src/http_upload_chunk_trial.cpp').read_text(),
                      SIGNATURE.replace('HTTPClient::', 'UploadHTTPClient::'))
    assert actual == wanted, 'Unreviewed HTTP method drift'
    assert sha(sdk / 'libraries/HTTPClient/src/HTTPClient.cpp') == CPP_SHA, 'Pinned SDK HTTP source changed'
    assert sha(sdk / 'libraries/HTTPClient/src/HTTPClient.h') == HEADER_SHA, 'Pinned SDK HTTP header changed'
    return dict(passed=True, original_sha256=CPP_SHA, sdk_header_sha256=HEADER_SHA,
                original_method_sha256=hashlib.sha256(original.encode()).hexdigest(),
                derivative_method_sha256=hashlib.sha256(actual.encode()).hexdigest(),
                sdk_modified=False, tx_original=1460, tx_trial_default=4096,
                reviewed_tx_bytes=[1460,4096],
                extra_transient_allocation_bytes_at_4096=2636, rx_unchanged=4096,
                scope='Pinned source/derivative comparison only; no physical performance claim')

def verify_link(environment, disabled=False):
    # Cross-reference provider checks are paired with the actual final ELF;
    # discarded map text by itself is never evidence that a method is linked.
    build = ROOT / '.pio/build' / environment
    elf = build / 'firmware.elf'
    mapping = (build / 'firmware.map').read_text()
    core = Path(os.environ.get('PLATFORMIO_CORE_DIR', '/config/.unmanic/research/cloud-stick-platformio'))
    nm = core / 'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-nm'
    output = subprocess.check_output([str(nm), '-n', str(elf)], text=True)
    symbols = {m.group(3): (m.group(1), m.group(2)) for line in output.splitlines()
               if (m := re.fullmatch(r'([0-9a-fA-F]+)\s+(\w)\s+(\S+)', line))}
    if disabled:
        assert SYMBOL not in symbols, 'Trial method present in disabled ELF'
    else:
        assert SYMBOL in symbols and symbols[SYMBOL][1] == 'T', 'Missing strong trial stream method'
        assert re.search(r'^' + SYMBOL + r'\s+[^\n]*src/http_upload_chunk_trial.cpp.o\s*$', mapping, re.M), 'Wrong stream provider'
        for method in ('begin', 'writeToStream', 'handleHeaderResponse'):
            # Original provider must remain reachable for inherited behavior.
            assert any('HTTPClient' in key and method in key for key in symbols), 'Inherited HTTP method missing: ' + method
    return dict(passed=True, environment=environment, disabled=disabled,
                elf_sha256=sha(elf), map_sha256=sha(build / 'firmware.map'),
                trial_method_present=not disabled,
                provider=None if disabled else 'src/http_upload_chunk_trial.cpp.o',
                inherited_sdk_still_linked=True if not disabled else None,
                scope='Final ELF and exact symbol-provider inspection only')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--env')
    parser.add_argument('--disabled', action='store_true')
    parser.add_argument('--diff', action='store_true')
    args = parser.parse_args()
    result = verify()
    if args.diff:
        original = function((BASE / 'HTTPClient.cpp.orig').read_text())
        print(''.join(difflib.unified_diff(original.splitlines(True), derivative(original).splitlines(True), fromfile='pinned original method', tofile='reviewed UploadHTTPClient method')))
    if args.env:
        result['link'] = verify_link(args.env, args.disabled)
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
