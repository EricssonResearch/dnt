# FRER-AoI Testbed on DNT — Documentation

Emulation testbed for the Letter's Fig. 1 scenario: a periodic source whose
stream is protected by IEEE 802.1CB FRER (replication over two lossy,
delay-disparate paths, then duplicate elimination) followed by a Packet
Ordering Function (POF) with hold time *D*, measuring **in-order
completeness** and **Age of Information (AoI)** at the listener.

Built on [DNT — Dependable Networking Toolkit](https://github.com/EricssonResearch/dnt)
(Ericsson Research), running four instances of Linux network namespaces on a
single host, with `tc netem` providing per-path delay distributions and loss.

**Unit mapping:** 1 model time unit = 1 ms, source period T = 1 unit
(λ = 1 kHz). All D/H below follow the Letter's Setup: D_h = D_t = 1 ms,
branch A (Dprop = 1 ms, β_A = 2 ms⁻¹, p_A = 0.97), branch B (Dprop = 3 ms,
β_B = 0.4 ms⁻¹, p_B = 0.99), mean skew E[S] = 4 ms, H_max = 16.

---

## 1. File inventory

| File | Purpose |
|---|---|
| `env_frer_aoi.sh` | One-time (per boot) environment builder: creates the 4 network namespaces, the veth links between them, and installs the `tc netem` impairments. Also generates the exponential delay distribution table for netem. Provides `setup` / `teardown` shell functions. **Must be sourced in bash, as root.** |
| `gen_expo_dist.py` | Generates `expo.dist`, the netem distribution table realizing an exponential residual delay W ~ Exp(1/mean). Called by `setup`. |
| `nxp1.ini` | DNT config for the **SGF node** (sequence generation + replication). |
| `nxp2.ini` | DNT config for the **recovery node**: SRF (duplicate elimination, history H) followed by POF (ordering, hold time D). `run_point.sh` rewrites `MaxDelay` and `frerSeqRcvyHistoryLength` in this file for each operating point. |
| `talker.py` | Periodic source: N frames at T = 1 ms (SCHED_FIFO via `chrt` when available, to keep source jitter ≪ T), 802.1Q VLAN 10, experimental ethertype 0x88B5, payload = 64-bit sequence number + 64-bit float generation timestamp (`CLOCK_REALTIME`). |
| `listener.py` | Sink + measurement: raw-socket capture of delivered frames, extracts (seq, gen_ts, rx_ts), enforces monotone sequence (out-of-order arrivals counted in `outOfOrder` and discarded, consistent with the model's "late discard"), computes completeness, time-average AoI, and mean peak AoI. Optional 2nd argument: dump the raw per-frame `(seq, gen, rx)` arrays to a compressed `.npz` for offline distributional metrics (PAoI CCDF). |
| `run_point.sh` | Runs **one operating point** `(D, H, N)`: rewrites `nxp2.ini`, (re)starts both DNT instances, launches listener then talker, prints the result line and stores it in `result_D<D>.txt`. Also saves the raw capture to `raw/D<D>_H<H>_<timestamp>.npz` (timestamped ⇒ repetitions never overwrite each other; override dir with `RAWDIR=`). |
| `verify_setup.sh` | Post-run sanity checker: dumps qdisc configs + drop counters and per-interface packet counters so replication / elimination / loss rates can be checked against theory (see §5). |
| `sweep.sh` | Runs the full D-sweep: for each D in the grid, H* = min(D, 16), R repetitions of `run_point.sh`, appending tagged result lines to `sweep_results.txt`. |
| `aggregate_results.py` | Parses `sweep_results.txt` → `emulation_points.csv` with per-(D,H) means and 95% Student-t confidence intervals, ready to overlay on the figure (see §7). |

### Who calls what

```
env_frer_aoi.sh::setup          (run once, as root, in bash)
 ├── gen_expo_dist.py  → writes <tc-lib-dir>/expo.dist
 ├── ip netns add / ip link add / ethtool   (topology)
 └── tc qdisc replace ... netem ...          (impairments)

run_point.sh <D> <H> <N>        (run per operating point, as root)
 ├── sed -i nxp2.ini             (MaxDelay=D, frerSeqRcvyHistoryLength=H)
 ├── ip netns exec nxp1  ../dnt nxp1.ini &      (SGF)
 ├── ip netns exec nxp2  ../dnt nxp2.ini &      (SRF + POF)
 ├── ip netns exec listener python3 listener.py N > result_D<D>.txt &
 └── ip netns exec talker   python3 talker.py  N
```

The `dnt` binary is resolved as: `$DNT` env var → `dnt` on PATH → `../dnt`
(the build in the repo root). `sudo` strips the user PATH, hence the explicit
fallback.

---

## 2. Topology and network configuration

```
                 ┌────────────┐  branch A   ┌───────────────┐
 ┌────────┐ head │   nxp1     ├─────────────┤     nxp2      │ tail ┌──────────┐
 │ talker ├──────┤   (SGF)    │             │  (SRF + POF)  ├──────┤ listener │
 └────────┘      │ Gen + Repl ├─────────────┤  Elim -> Ord  │      └──────────┘
   eth0       uni└────────────┘  branch B   └───────────────┘uni       eth0
                brA/brB                          brA/brB
```

Four namespaces, four veth pairs:

| Link | veth endpoints | Impairment (egress qdisc) | Model |
|---|---|---|---|
| head | talker:eth0 ↔ nxp1:uni | netem `delay 1ms` on talker:eth0 | D_h = 1 ms, lossless |
| branch A | nxp1:brA ↔ nxp2:brA | netem `delay 1.5ms 1ms distribution expo loss random 3%` on nxp1:brA | L_A = 1 ms + Exp(2 ms⁻¹), p_A = 0.97 |
| branch B | nxp1:brB ↔ nxp2:brB | netem `delay 5.5ms 5ms distribution expo loss random 1%` on nxp1:brB | L_B = 3 ms + Exp(0.4 ms⁻¹), p_B = 0.99 |
| tail | nxp2:uni ↔ listener:eth0 | netem `delay 1ms` on nxp2:uni | D_t = 1 ms, lossless |

netem's `delay MU SIGMA distribution expo` draws `MU + SIGMA·t/8192` with `t`
an int16 from `expo.dist`. The table stores `t = 8192·(Exp(1) − 1)/2`, so with
`SIGMA = 2·mean` and `MU = Dprop + mean` this realizes exactly
`L = Dprop + W, W ~ Exp(1/mean)` (branch A: Dprop = 1 ms, mean = 1/β_A =
0.5 ms; branch B: Dprop = 3 ms, mean = 1/β_B = 2.5 ms). The int16 clip
truncates the Exp tail at ≈ 9·mean (P ≈ 1.2e-4).

VLAN plumbing (how DNT classifies the streams):

- talker sends VID **10** → nxp1 matches VID 10, adds R-TAG (seq), replicates:
  copy A re-tagged VID **100** → brA, copy B re-tagged VID **200** → brB.
- nxp2 matches VID 100/200, reads the R-TAG seq, eliminates duplicates
  (SeqRcvy, Vector algorithm, history **H**), orders (Pof, `MaxDelay` **D** ms,
  `BufferSize` 64, `TakeAnyTime` 2000 ms), strips the R-TAG, re-tags VID 10,
  sends to the listener.
- `ethtool -K ... rxvlan off txvlan off` disables VLAN offload so tags stay
  in-packet; the listener still handles both tagged and kernel-stripped frames.

---

## 3. Bugs found and fixed along the way

### 3.1 Testbed-side (scripts)

| Symptom | Root cause | Fix |
|---|---|---|
| `exec of "dnt" failed: No such file or directory` | `sudo` resets PATH to `secure_path`, which does not contain the project dir | `run_point.sh` resolves the binary explicitly (`$DNT` → PATH → `../dnt`) |
| `dnt: invalid option -- 'c'` | This DNT build takes the config file as a **positional** argument, not `-c` | `dnt nxp1.ini` instead of `dnt -c nxp1.ini` |
| `IndexError` in listener when nothing is received | Empty capture array | Listener exits gracefully with `delivered=0 ... (too few frames received ...)` |
| Branch qdiscs silently absent (completeness ≈ 1.0, avgAoI ≈ 2.5u — below the physical floor of ~4u) | Two independent problems: (a) `expo.dist` was written to `/usr/lib/tc/` but Ubuntu's `tc` searches `/usr/lib/x86_64-linux-gnu/tc/`; (b) the table had 65536 entries while the kernel caps netem tables at 16384 (`MAX_DIST` in `sch_netem.c`) | `setup` auto-detects the tc lib dir (via the stock `normal.dist`); `gen_expo_dist.py` now emits **4096** exact Exp(1) quantiles (inverse CDF at midpoints), same size as stock tables |

**Diagnostic that exposed it:** with only head+tail netem active, avgAoI
measured exactly D_h + D_t + T/2 and completeness ≈ 1 — both inconsistent
with any run where the branch impairments are on. (The debugging above was
done under the original 10 ms bring-up configuration; the reasoning carries
over to the current 1 ms parameters unchanged.)

### 3.2 DNT source (`pof.c`) — 4 bugs, branch `fix-pof-deadline`

**Symptom:** continuous bursts of `[POF] [WARNING] buffer is full, dropping
new packet.` under sustained traffic; POF effectively dead.

**Mechanism:** when a sequence gap blocks in-order release, packets are held
in the conditional delay buffer until their `MaxDelay` deadline. Two bugs
prevented that deadline from ever firing while traffic kept flowing, so the
64-slot buffer filled and everything after that was dropped.

Commit 1 — `pof: fix get_next_deadline`:

1. **`get_next_deadline()` compared deadlines with `!=` instead of `<`**, so
   it returned the deadline of the *newest* packet instead of the earliest.
   Every new arrival pushed the timer further out → with inter-arrivals
   (≈10 ms) shorter than `MaxDelay` (43 ms) the timeout never fired.
2. `pof_try_forward()` dereferenced `next_to_forward` in a log statement
   *before* its NULL check.

Commit 2 — `pof: fire expired deadlines immediately, harden event handling`:

3. **Expired deadlines armed a 2 s timer.** The re-arm logic treated
   "deadline in the past" the same as "no deadline" and armed
   `take_any_time` (2000 ms) instead of firing immediately. Once any held
   packet's deadline passed while the queue was blocked, every new arrival
   re-armed 2 s again — deadlock persists under load. Fix: past deadline ⇒
   zero timeout (fire now).
4. **eventfd sums, code assumed OR-semantics.** Concurrent event
   notifications accumulate in the eventfd counter (1+1 = 2), so bit-testing
   the read value misclassifies two IN_ORDER events as OUT_OF_ORDER and skips
   the forward attempt. Fix: attempt a forward on any positive event
   (harmless when the head is not next-in-order). Plus an empty-queue guard
   in `pof_try_forward()`.

**Known remaining deviations (documented, not fixed):**

- On timeout DNT forwards the *oldest-arrival* packet, not the *lowest-seq*
  one (the code's own comment flags this as RFC-9550-conformant but wrong).
  Slightly inflates out-of-order deliveries; the listener's monotone-seq
  guard discards those, consistent with the model.
- POF tracks sequence numbers as 16-bit with no wraparound handling
  (`ntohl(seq) & 0xffff`) ⇒ **keep N < 65536 per run** (each run restarts
  DNT, so the counter starts fresh every time).

Git layout: `fix-pof-deadline` (the two fix commits only → PR to
EricssonResearch/dnt) and `frer-aoi-testbed` (= fixes + this testbed),
both on the fork `howhangliu/dnt`.

---

## 4. Running a measurement

```bash
# once per boot (bash, root):
sudo bash -c 'cd <this dir> && source env_frer_aoi.sh && setup'
# (rerun impairments after any teardown; watch for the netem WARNING — it must NOT appear)

# one operating point (D in ms = model units, H in packets, N frames):
sudo ./run_point.sh 4 4 60000         # ~60 s at 1000 pkt/s
```

Output (also saved to `result_D<D>.txt`):

```
delivered=...  completeness=...  avgAoI=...u  peakAoI=...u  outOfOrder=...
```

- **completeness** = delivered-in-order / N
- **avgAoI** = time-average age (trapezoid integration of the sawtooth), in
  model units (1u = 1 ms)
- **peakAoI** = mean of the sawtooth peaks `u_{k+1} − g_k`
- **outOfOrder** = frames that arrived non-monotonically and were discarded

## 5. Verification checklist

Run `sudo ./verify_setup.sh` after a run (counters accumulate across runs —
for exact bookkeeping do `teardown` + `setup` + one run first):

1. **Impairments live:** brA/brB show the netem configs; `dropped ≈ 3% / 1%`
   of the packets offered to each branch.
2. **Replication:** nxp1 brA TX+drops ≈ N *and* brB TX+drops ≈ N (two full
   copies of the stream).
3. **Elimination:** nxp2 uni TX ≈ delivered ≪ 2N (duplicates collapsed; only
   both-copies-lost and out-of-history/late packets missing).
4. **Ordering:** listener prints `outOfOrder≈0`. Falsification test: run
   `sudo ./run_point.sh 0 1 5000` (D=0 disables holding) → outOfOrder ≫ 0.
5. **Plausibility bounds:** completeness ≤ 1 − (1−p_A)(1−p_B) = 0.9997 (the
   gap below it is D/H-window loss — the phenomenon under study; the
   branch-recovery event "A lost, B delivered" has probability
   (1−p_A)·p_B ≈ 3×10⁻², i.e. ~1800 events per 60000-frame run);
   avgAoI ≥ D_h + Dprop_A + D_t + T/2 = 3.5u.

Benign log noise: `SYSMON ... pmc exited, status 253` (no PTP daemon — all
namespaces share the host clock, sync is irrelevant here); listener-side
`RX dropped` in `ip -s link` (ethertype 0x88B5 has no kernel handler; the
AF_PACKET tap still receives every frame).

## 6. Producing the full plot

**One `run_point.sh` invocation = one point** on the plot: it measures
(completeness, avgAoI, peakAoI) for a single (D, H) pair. With T = 1 ms the
conversion is direct:

- `MaxDelay = D` in ms = D in model units (integer ms only — DNT parses the
  value as an integer, so the emulation grid has 1 ms resolution even where
  the analytical D_ρ is fractional; place emulation markers at the integer
  D actually run)
- elimination history is **fixed at H_max = 16 across the sweep**: the
  model's Pareto front assumes ideal dedup, and the POF hold D does all the
  dropping (late frames are forwarded stale and discarded at the listener —
  the model's late-discard). Matching H to D adds non-model losses: DNT's
  Vector recovery *rogue-drops* out-of-window packets, and H ≤ 2 deadlocks
  into 2 s resets after a single branch-A loss (measured: D=1/H=1 gives
  completeness 0.0035). The coupling H\* = ceil(D_ρ/T) is validated
  separately at the star point: `run_point.sh 6 7` (expect comp ≥ ρ) vs the
  H_max=4 infeasibility demo `run_point.sh 6 4` (expect comp < ρ).

Run the whole sweep with:

```bash
sudo ./sweep.sh                                   # defaults: D in {0..6,8,10,12,16}, H=16, R=5, N=60000
D_LIST="2 4 6 8" R=10 N=60000 sudo -E ./sweep.sh  # custom grid / repetitions
```

**Budget:** at 1 kHz a 60000-frame run takes ~60 s (+ ~2 s restart overhead)
⇒ default grid 10 points × 5 reps ≈ **55 min** total. Runs are strictly
sequential — the namespaces are shared state; never parallelize on one host.

**Statistics:** per rep, the completeness standard error is
√(p(1−p)/N) ≈ 4×10⁻⁴ at p ≈ 0.99; R = 5 reps brings the point estimate to
≈ 2×10⁻⁴ — sufficient for the 10⁻³-scale features of ρ. Each rep contains
~1800 branch-recovery events ((1−p_A)p_B ≈ 3×10⁻²) and ~18 both-copies-lost
events. The simulator's 4×10⁶-update resolution is not reachable in a single
run (N < 65536, §3.2); if a point needs it, raise R (R = 67 ≈ 4×10⁶ samples,
~70 min for that one point).

## 7. Combining emulation results into the figure

1. Aggregate the sweep into per-point means ± 95% CI:

   ```bash
   python3 aggregate_results.py sweep_results.txt -o emulation_points.csv
   ```

2. Overlay on the existing analytical/simulation figure as discrete markers
   with error bars (do not draw lines through emulation points — they are
   measurements, not a model):

   ```python
   import pandas as pd
   em = pd.read_csv("emulation_points.csv")
   ax.errorbar(em.D, em.comp_mean, yerr=em.comp_ci, fmt="o", mfc="none",
               ms=5, capsize=2, color="k", label="DNT emulation", zorder=5)
   ax2.errorbar(em.D, em.avgAoI_mean, yerr=em.avgAoI_ci, fmt="o", mfc="none",
                ms=5, capsize=2, color="k", zorder=5)
   ```

   AoI columns are already in model units (1u = 1 ms), so no rescaling is
   needed against the analytical curves.

3. If the figure's x-axis is ρ rather than D, map each emulated integer D
   through the Letter's D_ρ relation (eq. Drho) to place the marker at its
   effective ρ; alternatively add a top axis in D.

3b. **PAoI CCDF (fig_paoi):** the Letter's CCDF figure compares the
   simulator's peak-AoI distribution (curves from `paoi_sweep.py` in the
   repo root → `paoi_ccdf.npz`) against pooled empirical CCDFs from the raw
   captures. Required emulation runs: repetitions at **D ∈ {0, 6, 16}, H = 16**
   (the three curve operating points — DROP, ≈D_ρ design point, HOLD):

   ```bash
   D_LIST="0 6 16" R=5 N=60000 sudo -E ./sweep.sh   # ~16 min, 5×60000 peaks per D
   ```

   The companion analysis script (`figs.py`, kept outside this repo)
   pools every `raw/D<D>_H16_*.npz` per D into the overlay markers;
   more reps ⇒ deeper reachable tail
   (pooled n samples resolve the CCDF down to ~1/n).

**Result-file lifecycle:** `sweep_results.txt` is append-only (dated header
per sweep); `result_D<D>.txt` holds only the *last* run at that D and is
overwritten; `raw/*.npz` files are timestamped and never overwritten.
Finished campaigns are preserved by copying `sweep_results.txt`,
`result_D*.txt`, `emulation_points.csv` (and optionally `raw/`) into
`results_archive/<date>_<label>/` — that directory is exempt from
`.gitignore`, so commit it to make the campaign durable.

4. Suggested caption note: "Markers: emulation on the DNT reference
   implementation (Linux network namespaces + netem), mean of R runs of
   N = 6×10⁴ updates, 95% CIs." Expect emulation points to sit close to the
   simulation with small systematic offsets — sources: integer-ms D
   granularity, netem's truncated-Exp tail (≈ 9·mean), source timing jitter
   (~tens of µs with SCHED_FIFO), and DNT's timeout-releases-oldest-arrival
   deviation (§3.2). If an emulation point deviates visibly, check
   `outOfOrder` and `verify_setup.sh` before suspecting the model.
