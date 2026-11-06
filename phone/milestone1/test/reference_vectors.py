"""Generate synthetic SRP vectors from the unmodified Espressif Python reference."""
import hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'reference'))
import srp6a
counter=0
def random_value(n):
    global counter
    counter+=1
    return int.from_bytes(hashlib.sha512(('CloudStick-synthetic-'+str(counter)).encode()).digest()[:n],'big')
srp6a.get_random=random_value
username,password='wifiprov','abcd1234'
client=srp6a.Srp6a(username,password)
salt=bytes.fromhex('112233445566778899aabbccddeeff00')
x=srp6a.calculate_x(hashlib.sha512,int.from_bytes(salt,'big'),username,password)
v=pow(client.g,x,client.N)
b=int.from_bytes(hashlib.sha256(b'CloudStick-synthetic-server').digest(),'big')
B=(client.k*v+pow(client.g,b,client.N))%client.N
M=client.process_challenge(salt,B.to_bytes(384,'big'))
HAMK=srp6a.calculate_H_AMK(hashlib.sha512,client.A,M,client.K)
client.verify_session(HAMK)
assert client.authenticated()
for i in range(4096):
    unsupported_password='synthetic-leading-zero-'+str(i)
    if hashlib.sha512((username+':'+unsupported_password).encode()).digest()[0]==0:break
else:raise RuntimeError('Could not generate credential encoding edge')
result={'source':'ESP-IDF v5.5.5 unmodified srp6a.py','synthetic':True,'username':username,'password':password,'a':client.a.to_bytes(32,'big').hex(),'A':client.A.to_bytes(384,'big').hex(),'salt':salt.hex(),'B':B.to_bytes(384,'big').hex(),'proof':M.hex(),'device_proof':HAMK.hex(),'key':client.K[:32].hex(),'nonce':'010203040506070800000001','unsupported_password':unsupported_password}
(Path(__file__).parent/'reference-vectors.json').write_text(json.dumps(result,indent=2)+'\n')
# Deterministically cover the SDK's legitimate shorter MPI serialization of B.
for i in range(4096):
    b=int.from_bytes(hashlib.sha256(('CloudStick-short-B-'+str(i)).encode()).digest(),'big')
    short_B=(client.k*v+pow(client.g,b,client.N))%client.N
    if (short_B.bit_length()+7)//8==383:break
else:raise RuntimeError('Could not generate deterministic short B')
short_M=client.process_challenge(salt,short_B.to_bytes(383,'big'))
short_HAMK=srp6a.calculate_H_AMK(hashlib.sha512,client.A,short_M,client.K)
short={**result,'B':short_B.to_bytes(383,'big').hex(),'proof':short_M.hex(),'device_proof':short_HAMK.hex(),'key':client.K[:32].hex(),'server_public_key_bytes':383}
(Path(__file__).parent/'reference-vectors-short-b.json').write_text(json.dumps(short,indent=2)+'\n')
print('Generated synthetic Espressif SRP reference vectors')
