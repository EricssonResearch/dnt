#!/usr/bin/env python3
"""Aggregate sweep_results.txt into per-(D,H) means with 95% CIs.

Usage:  python3 aggregate_results.py [sweep_results.txt] [-o emulation_points.csv]

Output CSV columns (AoI in model units, 1u = 1 ms):
  D,H,reps,comp_mean,comp_ci,avgAoI_mean,avgAoI_ci,peakAoI_mean,peakAoI_ci,ooo_mean

Overlaying on the Letter's figure (matplotlib):

  import pandas as pd
  em = pd.read_csv("emulation_points.csv")
  ax.errorbar(em.D, em.comp_mean, yerr=em.comp_ci, fmt="o", mfc="none",
              ms=5, capsize=2, color="k", label="DNT emulation", zorder=5)
  # same pattern with em.avgAoI_mean / em.peakAoI_mean on the AoI axis;
  # if the x-axis is rho instead of D, map each D through eq. (Drho) inverse.
"""
import re, sys, csv, math
from collections import defaultdict

# two-sided 95% Student-t quantiles, df = reps-1
T95 = {1: 12.71, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
       6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228}

def mean_ci(xs):
    n = len(xs)
    m = sum(xs) / n
    if n < 2:
        return m, float("nan")
    s = math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1))
    t = T95.get(n - 1, 1.96)
    return m, t * s / math.sqrt(n)

src = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "sweep_results.txt"
out = sys.argv[sys.argv.index("-o") + 1] if "-o" in sys.argv else "emulation_points.csv"

pat = re.compile(
    r"D=(\d+)\s+H=(\d+)\s+rep=\d+\s+delivered=\d+\s+completeness=([\d.]+)\s+"
    r"avgAoI=([\d.]+)u\s+peakAoI=([\d.]+)u\s+outOfOrder=(\d+)")

pts = defaultdict(lambda: {"comp": [], "avg": [], "peak": [], "ooo": []})
for line in open(src):
    m = pat.search(line)
    if not m:
        continue
    k = (int(m.group(1)), int(m.group(2)))
    pts[k]["comp"].append(float(m.group(3)))
    pts[k]["avg"].append(float(m.group(4)))
    pts[k]["peak"].append(float(m.group(5)))
    pts[k]["ooo"].append(int(m.group(6)))

with open(out, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["D", "H", "reps", "comp_mean", "comp_ci",
                "avgAoI_mean", "avgAoI_ci", "peakAoI_mean", "peakAoI_ci", "ooo_mean"])
    for (D, H) in sorted(pts):
        p = pts[(D, H)]
        cm, cc = mean_ci(p["comp"]); am, ac = mean_ci(p["avg"]); pm, pc = mean_ci(p["peak"])
        w.writerow([D, H, len(p["comp"]), f"{cm:.5f}", f"{cc:.5f}",
                    f"{am:.4f}", f"{ac:.4f}", f"{pm:.4f}", f"{pc:.4f}",
                    f"{sum(p['ooo'])/len(p['ooo']):.1f}"])
print(f"wrote {out}: {len(pts)} operating points")
