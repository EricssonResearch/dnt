#!/usr/bin/env python3
"""Generate a netem distribution table for exponential residual delay.

netem draws delay = mu + sigma * t/8192 with t an int16 from the table.
We store t = 8192 * (Exp(1) - 1) / 2 and use sigma = 2 * mean, so
delay = mu - mean + mean*Exp(1). With mu = Dprop + mean the netem line

    delay (Dprop+mean) (2*mean) distribution expo

realizes L = Dprop + W, W ~ Exp(1/mean), exactly eq. (3) of the Letter.
int16 clipping truncates the Exp tail at ~9*mean (P ~ 1.2e-4).

Table size: kernel netem rejects tables > 16384 entries (MAX_DIST in
sch_netem.c); stock iproute2 tables use 4096, so we do too. Entries are
exact Exp(1) quantiles (inverse CDF at midpoints), not random draws.
"""
import numpy as np, os, sys

n = 4096
p = (np.arange(n) + 0.5) / n                       # CDF midpoints
x = (-np.log1p(-p) - 1.0) / 2.0                    # (Exp(1) quantile - 1)/2
t = np.clip(np.round(x * 8192), -32768, 32767).astype(int)
out = sys.argv[1] if len(sys.argv) > 1 else "/usr/lib/tc/expo.dist"
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as f:
    for i in range(0, n, 8):
        f.write(" ".join(f"{v:6d}" for v in t[i:i+8]) + "\n")
print(f"wrote {out}: mean={t.mean()/8192:.4f} (target 0), "
      f"sigma-units, max={t.max()/8192:.2f}")