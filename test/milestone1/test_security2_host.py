"""Negative protocol cases against the actual vendored Espressif SDK sources.

No BLE/radio or firmware is executed. The stdin adapter bounds requests before
the official parser, mirroring the need for a production transport bound.
"""
import json
import os
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/milestone1/host-security2'


def varint(n):
    result=bytearray()
    while n>127:result.append((n&127)|128);n>>=7
    result.append(n);return bytes(result)


def blob(field,payload):return varint((field<<3)|2)+varint(len(payload))+payload


def sc0(username,key):
    cmd=blob(1,username)+blob(2,key)
    payload=blob(20,cmd) # msg=0 defaults to Command0.
    return b'\x10\x02'+blob(12,payload)


def main():
    env=dict(os.environ);env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1:abort_on_error=1'
    env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    child=subprocess.Popen([str(OUT/'security2-host-asan')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
    count=0;cases=[]
    def request(command,payload=b''):
        child.stdin.write(command+(' '+payload.hex() if payload else '')+'\n');child.stdin.flush()
        line=child.stdout.readline()
        if not line:raise AssertionError('SDK host stopped: '+child.stderr.read())
        return json.loads(line)
    def rejected(name,command,payload):
        nonlocal count
        response=request(command,payload);assert response['error']!=0,(name,response)
        assert response['data']=='',(name,response);count+=1;cases.append(name)
    rejected('encryption before ownership proof','encrypt',b'info')
    rejected('decryption before ownership proof','decrypt',bytes(17))
    rejected('short GCM frame bounded by adapter','decrypt',bytes(15))
    rejected('oversize frame bounded by adapter','session',bytes(1025))
    rejected('malformed protobuf','session',b'\xff')
    rejected('security0 downgrade','session',b'\x10\x00')
    rejected('security1 downgrade','session',b'\x10\x01')
    rejected('security2 missing payload','session',b'\x10\x02')
    rejected('security2 missing command','session',b'\x10\x02'+blob(12,b''))
    rejected('client public key short','session',sc0(b'wifiprov',bytes(383)))
    rejected('client username empty','session',sc0(b'',bytes(384)))
    rejected('invalid SRP public key A=0','session',sc0(b'wifiprov',bytes(384)))
    for i in range(100):
        result=request('reset');assert result['error']==0,result
    count+=100;cases.append('100 repeated transport session resets without authentication')
    child.stdin.write('quit\n');child.stdin.flush();stdout,stderr=child.communicate(timeout=10)
    assert child.returncode==0,(child.returncode,stderr)
    # Even recovering diagnostics from a misconfigured toolchain must fail.
    # This harness expects no SDK stderr: log macros in host_shims are silent.
    assert not stderr,stderr
    result={'passed':True,'checks':count,'cases':cases,'scope':'Pinned IDF v5.5.5 Security2 with shared reviewed alignment correction/SRP/protobuf on desktop mbedTLS; input adapter bounds tested separately from firmware','sanitizers':'Strict nonrecovering ASan/UBSan with leak detection and stderr rejection','physical_device_test':False}
    (OUT/'negative-tests.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
