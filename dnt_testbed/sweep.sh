#!/bin/bash
# Sweep the POF hold time D (ms): one run_point.sh call per (D, rep).
# Run as root:  sudo ./sweep.sh
# Override defaults via env:  D_LIST="2 4 6" R=10 N=60000 sudo -E ./sweep.sh
#
# Elimination history is FIXED at Hmax=16 across the sweep: the model's
# Pareto front uses ideal dedup, and the POF hold-time D does all the
# dropping (late frames are forwarded stale and discarded at the listener,
# = the model's late-discard). Matching H to D would add non-model losses:
# DNT's Vector recovery rogue-drops out-of-window packets, and H<=2 even
# deadlocks into 2 s resets after a single branch-A loss.
# The coupling H* = ceil(D_rho/T) is validated separately at the star point:
#   sudo ./run_point.sh 6 7 60000   (rho=0.995 design point, expect comp>=rho)
#   sudo ./run_point.sh 6 4 60000   (Hmax=4 infeasibility demo, comp<rho)
cd "$(dirname "${BASH_SOURCE[0]}")"
D_LIST=${D_LIST:-"0 1 2 3 4 5 6 8 10 12 16"}
R=${R:-5}
N=${N:-60000}          # keep < 65536 (16-bit R-TAG seq in DNT's POF)
H=${H_FIXED:-16}
OUT=${OUT:-sweep_results.txt}

echo "# sweep $(date -Is)  N=$N R=$R H=$H D_LIST=[$D_LIST]" >> "$OUT"
for D in $D_LIST; do
  for r in $(seq 1 "$R"); do
    line=$(./run_point.sh "$D" "$H" "$N" | tail -1)
    echo "D=$D H=$H rep=$r  $line" | tee -a "$OUT"
  done
done
echo "done -> $OUT  (aggregate with: python3 aggregate_results.py $OUT)"
