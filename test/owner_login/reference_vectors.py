"""Public synthetic login-record vectors from the pinned official SRP reference.

No random/production credentials, files or device are read. The fixture salt is
deterministic test input only and must never be used for a real owner login.
"""
import hashlib
import importlib.util
import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / 'phone/milestone1/test/reference/srp6a.py'
REFERENCE_SHA = '9dc802e1540cce95220479cf1c56a836beeeba0869117fd3904008ac67f5537c'
assert hashlib.sha256(REFERENCE.read_bytes()).hexdigest() == REFERENCE_SHA
sys.path.insert(0, str(REFERENCE.parent))
spec = importlib.util.spec_from_file_location('official_owner_login_srp', REFERENCE)
srp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(srp)
N, g = srp.get_ng(srp.NG_3072)
device = '001122334455'
salt = bytes(range(0x20, 0x30))

def record(username, password):
    # Call the official implementation with its deterministic test entropy hook.
    srp.get_random = lambda length: int.from_bytes(salt, 'big')
    actual_salt, verifier = srp.generate_salt_and_verifier(username, password, 16)
    assert actual_salt == salt and len(verifier) == 384
    body = bytearray(432)
    body[:4] = b'COW2'
    body[4] = 2
    body[6:12] = bytes.fromhex(device)
    body[12:28] = salt
    body[28:412] = verifier
    struct.pack_into('<I', body, 428, zlib.crc32(body[:428]))
    return dict(username=username, password=password, device=device, salt=salt.hex(),
                verifier=verifier.hex(), record=body.hex(), sha256=hashlib.sha256(body).hexdigest())

vectors = []
for username, password in [('admin', 'PUBLIC-SYNTHETIC-LOGIN-2026'),
                           ('owner.name_2', 'PUBLIC-été-password-2026'),
                           ('A' * 32, 'P' * 128), ('csp-owner', 'CSP-PUBLIC-SYNTHETIC-OWNER-ONLY-2026')]:
    assert hashlib.sha512((username + ':' + password).encode()).digest()[0]
    vectors.append(record(username, password))

for suffix in range(10000):
    rare_password = 'PUBLIC-EDGE-PASSWORD-' + str(suffix)
    inner = hashlib.sha512(('admin:' + rare_password).encode()).digest()
    if inner[0] == 0:
        reference_verifier = pow(g, srp.calculate_x(hashlib.sha512, salt, 'admin', rare_password), N).to_bytes(384, 'big')
        sdk_x = int.from_bytes(hashlib.sha512(salt + inner).digest(), 'big')
        sdk_verifier = pow(g, sdk_x, N).to_bytes(384, 'big')
        assert reference_verifier != sdk_verifier
        rare = dict(username='admin', password=rare_password, inner_sha512=inner.hex(),
                    reference_verifier=reference_verifier.hex(), sdk_raw_verifier=sdk_verifier.hex(),
                    expected='explicit compatibility rejection; no replacement record')
        break
else:
    raise AssertionError('No synthetic rare fixture found')

output = dict(synthetic_only=True, reference_sha256=REFERENCE_SHA,
              protocol='Espressif Security2 / SRP3072 / SHA512 / existing COW2',
              vectors=vectors, rare_inner_zero=rare)
path = Path(__file__).with_name('reference-vectors.json')
path.write_text(json.dumps(output, indent=2) + '\n')
print('Prepared four public official-reference COW2 golden vectors and one leading-zero incompatibility fixture')
