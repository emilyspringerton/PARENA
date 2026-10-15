#!/usr/bin/env python3
"""Lossy UDP forwarder for the QUIC client tests.
usage: udp_proxy.py <listen-port> <server-port> [--drop-c2s i,j,..] [--drop-s2c-every N] [--reorder-s2c]
Datagram indices count from 0 per direction. --reorder-s2c swaps each adjacent pair of server datagrams."""
import socket, sys, select
lp, sp = int(sys.argv[1]), int(sys.argv[2])
drop_c = set(); every = 0; reorder = False
a = sys.argv[3:]
while a:
    if a[0] == "--drop-c2s": drop_c = {int(x) for x in a[1].split(",")}; a = a[2:]
    elif a[0] == "--drop-s2c-every": every = int(a[1]); a = a[2:]
    elif a[0] == "--reorder-s2c": reorder = True; a = a[1:]
    else: a = a[1:]
cs = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); cs.bind(("127.0.0.1", lp))
ss = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
client = None; ci = 0; si = 0; held = None
print("READY", flush=True)
while True:
    r, _, _ = select.select([cs, ss], [], [], 1.0)
    for s in r:
        data, addr = s.recvfrom(4096)
        if s is cs:
            client = addr
            if ci in drop_c: ci += 1; continue
            ci += 1
            ss.sendto(data, ("127.0.0.1", sp))
        else:
            n = si; si += 1
            if every and n % every == every - 1: continue
            if client is None: continue
            if reorder:
                if held is None: held = data; continue
                cs.sendto(data, client); cs.sendto(held, client); held = None
            else:
                cs.sendto(data, client)
    if reorder and held is not None and not r and client:
        cs.sendto(held, client); held = None
