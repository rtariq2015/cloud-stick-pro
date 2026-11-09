# SPDX-License-Identifier: MIT
"""Credential-free local discovery; fixed public-status reads, never redirects."""
import http.client
import ipaddress
import secrets
import socket
import time
import re


def address(value):
    if type(value) is not str or len(value)>15: raise ValueError()
    ip=ipaddress.IPv4Address(value)
    if str(ip)!=value or not any(ip in ipaddress.IPv4Network(n) for n in ('10.0.0.0/8','172.16.0.0/12','192.168.0.0/16')): raise ValueError()
    return value


def discover():
    nonce=secrets.token_hex(16).encode('ascii');found=set()
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as channel:
        channel.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)
        channel.bind(('',0));channel.settimeout(.2)
        deadline=time.monotonic()+3;next_send=0
        while time.monotonic()<deadline and len(found)<16:
            if time.monotonic()>=next_send:
                channel.sendto(b'CSP-DISCOVER/1 '+nonce,('255.255.255.255',37219));next_send=time.monotonic()+1
            try:
                raw,peer=channel.recvfrom(128)
                if raw==b'CSP-FOUND/1 '+nonce and peer[1]==37219: found.add(address(peer[0]))
            except (socket.timeout,ValueError): pass
    return sorted(found)


def status(value):
    host=address(value);connection=http.client.HTTPConnection(host,80,timeout=2)
    try:
        connection.request('GET','/status',headers={'Accept':'application/json','Connection':'close'})
        reply=connection.getresponse()
        if reply.status!=200 or reply.getheader('Content-Type','').split(';')[0].strip().lower()!='application/json': raise ValueError()
        from .firmware_catalog import strict_json
        data=strict_json(reply.read(4097),4096)
        flags={'wifi','media','active','scanning','has_scan','scan_complete','window_open','paused','cloud','configured'}
        integers={'uptime_ms','verified','pending','failed','bytes_per_second','quiet_seconds','last_backup_unix','error','percent','phase'}
        if set(data)!=flags|integers|{'firmware','done','total'}: raise ValueError()
        if any(type(data[k]) is not bool for k in flags) or any(type(data[k]) is not int or not -(2**31)<=data[k]<2**53 for k in integers): raise ValueError()
        if any(data[k]<0 for k in integers-{'error','percent'}) or not -1<=data['percent']<=100 or not 0<=data['phase']<=4: raise ValueError()
        if type(data['firmware']) is not str or not re.fullmatch('[A-Za-z0-9.-]{1,48}',data['firmware']): raise ValueError()
        for key in ('done','total'):
            if type(data[key]) is not int or not 0<=data[key]<=2**64-1: raise ValueError()
            # The wire uses unsigned JSON numbers. Keep all 64 bits when the
            # value subsequently crosses Qt's JSON/JavaScript boundary.
            data[key]=str(data[key])
        return data
    finally: connection.close()
