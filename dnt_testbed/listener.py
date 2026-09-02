#!/usr/bin/env python3
"""Receiver: logs (seq, gen_ts, rx_ts) per delivered frame; computes in-order
completeness and time-average / mean-peak in-order AoI.
Run: ip netns exec listener python3 listener.py N [raw.npz] > result.txt
Optional 2nd arg: dump the raw per-frame (seq, gen, rx) arrays to a
compressed .npz so distributional metrics (PAoI CCDF) can be computed
offline; timestamps stay in CLOCK_REALTIME seconds."""
import socket, struct, time, sys
import numpy as np

N = int(sys.argv[1]) if len(sys.argv) > 1 else 60_000
RAW = sys.argv[2] if len(sys.argv) > 2 else None
ETH_P_ALL = 3
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
s.bind(("eth0", 0))
s.settimeout(10.0)

rec = []
last = -1
ooo = 0    # frames arriving out of order / duplicated (POF+SRF should make this 0)
try:
    while True:
        try:
            f = s.recv(2048)
        except socket.timeout:
            break
        rx = time.clock_gettime(time.CLOCK_REALTIME)
        # kernel may strip the 802.1Q tag into metadata on RX:
        # tagged frame: eth | 8100 tci | 88b5 | payload  -> payload at 18
        # stripped    : eth | 88b5 | payload             -> payload at 14
        if len(f) < 30:
            continue
        if f[12:14] == b"\x81\x00" and f[16:18] == b"\x88\xb5":
            off = 18
        elif f[12:14] == b"\x88\xb5":
            off = 14
        else:
            continue
        if len(f) < off + 16:
            continue
        seq, gen = struct.unpack("!Qd", f[off:off+16])
        if seq <= last:            # POF guarantees order; guard anyway
            ooo += 1
            continue
        last = seq
        rec.append((seq, gen, rx))
        if seq >= N - 1:
            break
finally:
    s.close()

if RAW and rec:
    a = np.array(rec, dtype=np.float64)
    np.savez_compressed(RAW, seq=a[:, 0].astype(np.uint32),
                        gen=a[:, 1], rx=a[:, 2])
    print(f"raw samples -> {RAW} ({len(rec)} frames)", file=sys.stderr)

if len(rec) < 2:
    print(f"delivered={len(rec)}  completeness={len(rec)/N:.4f}  "
          f"avgAoI=nan  peakAoI=nan  "
          f"(too few frames received -- is dnt forwarding? check namespaces/impairments)")
    sys.exit(0)

seq = np.array([r[0] for r in rec]); g = np.array([r[1] for r in rec])
u = np.array([r[2] for r in rec])
comp = len(rec) / N
A = u - g; dt = np.diff(u)
avg = float(np.sum(A[:-1] * dt + 0.5 * dt**2) / (u[-1] - u[0]))
peak = float(np.mean(u[1:] - g[:-1]))
# report in model time units (1 unit = 1 ms)
print(f"delivered={len(rec)}  completeness={comp:.4f}  "
      f"avgAoI={avg/0.001:.3f}u  peakAoI={peak/0.001:.3f}u  outOfOrder={ooo}")