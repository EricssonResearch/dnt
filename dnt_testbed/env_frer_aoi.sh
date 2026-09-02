#!/bin/bash
# FRER_AoI Letter scenario on DNT (github.com/EricssonResearch/dnt)
# Topology mirrors Fig. 1 of the manuscript:
#
#   talker ──head── nxp1 (SGF) ══branch A══ nxp2 (SRF+POF) ──tail── listener
#                              ══branch B══
#
# Unit mapping: 1 model time unit = 1 ms  =>  T = 1 ms (1000 pkt/s)
#   head/tail:  Dh = Dt = 1 ms  -> netem delay 1ms, no loss
#   branch A :  Dprop=1ms, W~Exp(2/ms),   pA=0.97 -> delay (1+0.5)ms sigma 1ms expo, loss 3%
#   branch B :  Dprop=3ms, W~Exp(0.4/ms), pB=0.99 -> delay (3+2.5)ms sigma 5ms expo, loss 1%
# (mean of Exp(beta) = 1/beta = 0.5 ms and 2.5 ms; sigma = 2*mean, see gen_expo_dist.py)
#
# Run as root:  source env_frer_aoi.sh ; setup
set -e
export PATH=$PATH:/sbin:/usr/sbin

setup() {
  # exponential netem table -- must land in the dir tc actually searches
  # (Debian/Ubuntu: /usr/lib/x86_64-linux-gnu/tc; others: /usr/lib/tc).
  # Detect via a stock table; TC_LIB_DIR env var overrides for both us and tc.
  tclib="${TC_LIB_DIR:-$(dirname "$(find /usr/lib -name normal.dist 2>/dev/null | head -1)" 2>/dev/null)}"
  [ -d "$tclib" ] || tclib=/usr/lib/tc
  python3 "$(dirname "${BASH_SOURCE[0]}")/gen_expo_dist.py" "$tclib/expo.dist"

  for ns in talker listener nxp1 nxp2; do ip netns add $ns 2>/dev/null || true; done

  ip link add eth0 netns talker   type veth peer uni  netns nxp1     # head
  ip link add brA  netns nxp1     type veth peer brA  netns nxp2     # branch A
  ip link add brB  netns nxp1     type veth peer brB  netns nxp2     # branch B
  ip link add uni  netns nxp2     type veth peer eth0 netns listener # tail

  for ns in talker listener nxp1 nxp2; do
    ip netns exec $ns ip link set lo up
    for d in $(ip netns exec $ns ls /sys/class/net | grep -v lo); do
      ip netns exec $ns ip link set $d up
      ip netns exec $ns ethtool -K $d rx off tx off rxvlan off txvlan off 2>/dev/null || true
    done
  done

  # ---- impairments (egress qdiscs on the nxp1 side, head on talker side) ----
  # head: Dh = 1 ms, lossless
  netem_warn=0
  ip netns exec talker tc qdisc replace dev eth0 root netem delay 1ms limit 10000 || netem_warn=1
  # branch A: L_A = 1ms + Exp(mean 0.5ms), loss 3%
  ip netns exec nxp1 tc qdisc replace dev brA root netem \
      delay 1.5ms 1ms distribution expo loss random 3% limit 10000 || netem_warn=1
  # branch B: L_B = 3ms + Exp(mean 2.5ms), loss 1%
  ip netns exec nxp1 tc qdisc replace dev brB root netem \
      delay 5.5ms 5ms distribution expo loss random 1% limit 10000 || netem_warn=1
  # tail: Dt = 1 ms, lossless
  ip netns exec nxp2 tc qdisc replace dev uni root netem delay 1ms limit 10000 || netem_warn=1

  [ "$netem_warn" = 1 ] && echo "WARNING: netem unavailable (container?); impairments NOT applied"
  echo "namespaces up. start DNT:"
  echo "  ip netns exec nxp1 dnt nxp1.ini &"
  echo "  ip netns exec nxp2 dnt nxp2.ini &   # edit MaxDelay/HistoryLength per sweep point"
}

teardown() { for ns in talker listener nxp1 nxp2; do ip netns del $ns 2>/dev/null || true; done; }