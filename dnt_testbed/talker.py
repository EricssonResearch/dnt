#!/usr/bin/env python3
"""Periodic source: N frames at T=1 ms, 802.1Q vid=10, payload = seq + gen timestamp.
Run: ip netns exec talker python3 talker.py [N]
(pin timing jitter down with: chrt -f 80 python3 talker.py N)"""
import socket, struct, time, sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 60_000
T = 0.001
ETH_P_ALL = 3
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
s.bind(("eth0", 0))

dst = bytes.fromhex("ffffffffffff")
src = bytes.fromhex("020000000001")
vlan = struct.pack("!HH", 0x8100, 0x000A) + struct.pack("!H", 0x88B5)  # vid=10, exp ethertype

t0 = time.clock_gettime(time.CLOCK_REALTIME)
for k in range(N):
    target = t0 + k * T
    while True:
        now = time.clock_gettime(time.CLOCK_REALTIME)
        if now >= target: break
        time.sleep(min(0.0003, target - now))
    payload = struct.pack("!Qd", k, now) + b"\x00" * 30
    s.send(dst + src + vlan + payload)
print(f"sent {N} frames at {1/T:.0f} Hz")