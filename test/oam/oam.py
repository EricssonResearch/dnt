#!/usr/bin/python3

from mininet.net import Mininet
from threading import Thread
from mininet.cli import CLI
from select import *
from utils import *
from pprint import pprint
import regex as re
import time
import json
import sys


def create_net():
    """
    For single- and multi-stage PREOF OAM testing.
                      ---n2---
                     /   ||   \
        talker ---- n1---n3---n4----listener
    192.168.1.1                    192.168.1.2

    Router-local IPs for OAM: 10.0.0.{1,2,3,4}/32
    """
    net = Mininet(autoStaticArp=True)

    # nodes: a, b, c, d, talker, listener
    talker   = net.addHost('talker', ip='192.168.1.1/24')
    listener = net.addHost('listener', ip='192.168.1.2/24')
    n1 = net.addHost('n1', ip=None)
    n2 = net.addHost('n2', ip=None)
    n3 = net.addHost('n3', ip=None)
    n4 = net.addHost('n4', ip=None)

    # links
    net.addLink(talker, n1, intfName1='eth0', intfName2='eth0')
    net.addLink(n1, n2, intfName1='eth12', intfName2='eth21')
    net.addLink(n1, n3, intfName1='eth13', intfName2='eth31')
    net.addLink(n2, n3, intfName1='eth23', intfName2='eth32')
    # Uncomment line below for multi-stage PREOF network
    # net.addLink(n2, n3, intfName1='eth230', intfName2='eth320') #redundant
    net.addLink(n2, n4, intfName1='eth24', intfName2='eth42')
    net.addLink(n3, n4, intfName1='eth34', intfName2='eth43')
    net.addLink(listener, n4, intfName1='eth0', intfName2='eth0')

    net.build()
    return net

def config_net(net):
    t, l, n1, n2, n3, n4 = [net.get(n) for n in ["talker", "listener", "n1", "n2", "n3", "n4"]]
    ip_lo = 1
    for n in [n1, n2, n3, n4]:
        n.cmd(f"ip a a 10.0.0.{ip_lo}/32 dev lo")
        n.cmd("sysctl -w net.ipv4.ip_forward=1")
        ip_lo += 1
    # t.cmd("ip a a 192.168.1.1/24 dev eth0")
    # l.cmd("ip a a 192.168.1.2/24 dev eth0")
    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")

    n1.cmd("ip a a 12.0.0.1/24 dev eth12")
    n1.cmd("ip a a 13.0.0.1/24 dev eth13")
    n2.cmd("ip a a 12.0.0.2/24 dev eth21")
    n2.cmd("ip a a 23.0.0.2/24 dev eth23")
    n2.cmd("ip a a 24.0.0.2/24 dev eth24")
    n3.cmd("ip a a 13.0.0.3/24 dev eth31")
    n3.cmd("ip a a 23.0.0.3/24 dev eth32")
    n3.cmd("ip a a 34.0.0.3/24 dev eth34")
    n4.cmd("ip a a 24.0.0.4/24 dev eth42")
    n4.cmd("ip a a 34.0.0.4/24 dev eth43")

    # routing
    n1.cmd("ip r a 24.0.0.0/24 via 12.0.0.2 metric 5")
    n1.cmd("ip r a 24.0.0.0/24 via 13.0.0.3 metric 10")
    n1.cmd("ip r a 34.0.0.0/24 via 13.0.0.3 metric 5")
    n1.cmd("ip r a 34.0.0.0/24 via 12.0.0.2 metric 10")
    n3.cmd("ip r a 24.0.0.0/24 via 23.0.0.2 metric 5")
    n4.cmd("ip r a 12.0.0.0/24 via 24.0.0.2 metric 5")
    n4.cmd("ip r a 12.0.0.0/24 via 34.0.0.3 metric 10")
    n4.cmd("ip r a 13.0.0.0/24 via 34.0.0.3 metric 5")
    n4.cmd("ip r a 13.0.0.0/24 via 24.0.0.2 metric 10")

    # for OAM router-local address availability
    n1.cmd("ip r a default via 12.0.0.2")
    n2.cmd("ip r a default via 24.0.0.4")
    n3.cmd("ip r a default via 13.0.0.1")
    n4.cmd("ip r a default via 34.0.0.3")
    n2.cmd("ip r add 10.0.0.1/32 via 12.0.0.1")
    n3.cmd("ip r add 10.0.0.1/32 via 13.0.0.1")
    n4.cmd("ip r add 10.0.0.1/32 via 34.0.0.3")

    # delay
    n1.cmd("tc qdisc add dev eth13 root netem delay 30ms")
    n1.cmd("tc qdisc add dev eth12 root netem delay 5ms")
    n2.cmd("tc qdisc add dev eth24 root netem delay 30ms")
    n2.cmd("tc qdisc add dev eth23 root netem delay 5ms")
    n2.cmd("tc qdisc add dev eth21 root netem delay 5ms")
    n3.cmd("tc qdisc add dev eth31 root netem delay 10ms")
    n4.cmd("tc qdisc add dev eth43 root netem delay 15ms")
    # n3.cmd("tc qdisc add dev eth34 root netem delay 10ms")

def start_r2dtwos(net, debug):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo oam/singlestage/{n}.cfg -h {n}")
        else:
            # node.popen(f"xterm -T {n} -e env -i gdb -nx -ex=r --args ../r2dtwo oam/singlestage/{n}.cfg -vALL:NONE")
            node.popen(f"../r2dtwo oam/singlestage/{n}.cfg -vALL:NONE -h {n}")

# list of (sender node, telnet command, session id, expected reply)
testcases = [
    ('n1', 'list', 0,
"""
Available MEP Start points:
s1n1-e3-12 in tx112 type MEP-Start level 3 PseudoWire (pipe tx112 idx 0)
s1n1-e4-01 in s1 type MEP-Start level 4 PseudoWire (pipe s1 idx 7)
s2n1-i5-21 in rx242 type MIP level 5 PseudoWire (pipe rx242 idx 2)
s2n1-i5-31 in rx243 type MIP level 5 PseudoWire (pipe rx243 idx 2)
s3n1-e4-01 in s3 type MEP-Start level 4 PseudoWire (pipe s3 idx 5)
"""),
    ('n2', 'list', 0,
"""
Available MEP Start points:
s1n2-i3-12 in rx12 type MIP level 3 PseudoWire (pipe rx12 idx 2)
"""),
    ('n3', 'list', 0,
"""
Available MEP Start points:
s1n3-i4-13 in rx113 type MIP level 4 PseudoWire (pipe rx113 idx 2)
s1n3-i4-23 in rx123 type MIP level 4 PseudoWire (pipe rx123 idx 3)
s1n3-i4-34 in s1rx_cont type MIP level 4 PseudoWire (pipe rx123 idx 7)
s3n3-e1-32 in tx332 type MEP-Start level 1 PseudoWire (pipe tx332 idx 0)
s3n3-i4-13 in rx313 type MIP level 4 PseudoWire (pipe rx313 idx 2)
"""),
    ('n4', 'list', 0,
"""
Available MEP Start points:
s1n4-i4-24 in rx124 type MIP level 4 PseudoWire (pipe rx124 idx 3)
s1n4-i4-34 in rx134 type MIP level 4 PseudoWire (pipe rx134 idx 2)
s2n4-e5-04 in s2 type MEP-Start level 5 PseudoWire (pipe s2 idx 5)
s3n4-i4-24 in rx324 type MIP level 4 PseudoWire (pipe rx324 idx 3)
s3n4-i4-34 in rx334 type MIP level 4 PseudoWire (pipe rx334 idx 2)
"""),
    ('n1', 'returns', 0,
"""
Available OAM return interfaces:
  oam0 ip 10.0.0.1 port 6634 (default for UDP)
"""),
    ('n2', 'returns', 0,
"""
Available OAM return interfaces:
  oam0 ip 10.0.0.2 port 6634 (default for UDP)
"""),
    ('n3', 'returns', 0,
"""
Available OAM return interfaces:
  oam0 ip 10.0.0.3 port 6634 (default for UDP)
"""),
    ('n4', 'returns', 0,
"""
Available OAM return interfaces:
  oam0 ip 10.0.0.4 port 6634 (default for UDP)
"""),
    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3', 2,
"""
ping s1n1-e4-01 -> s1n2-i3-12 stream s1 session <session> level 3 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n2-i3-12 [target s1n2-i3-12] stream s1 session <session> level 3 seq 0
"""),
    ('n1', 'ping@oam0 s1n1-e4-01 s1n2-i3-12 3', 3,
"""
ping s1n1-e4-01 -> s1n2-i3-12 stream s1 session <session> level 3 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n2-i3-12 [target s1n2-i3-12] stream s1 session <session> level 3 seq 0
"""),
    ('n1', 'ping@10.0.0.1:6634 s1n1-e4-01 s1n2-i3-12 3', 4,
"""
ping s1n1-e4-01 -> s1n2-i3-12 stream s1 session <session> level 3 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n2-i3-12 [target s1n2-i3-12] stream s1 session <session> level 3 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3 -n 3 -i 0.001', 5,
"""
ping s1n1-e4-01 -> s1n2-i3-12 stream s1 session <session> level 3 count 3 interval 2 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n2-i3-12 [target s1n2-i3-12] stream s1 session <session> level 3 seq 0
  ping reply from s1n2-i3-12 [target s1n2-i3-12] stream s1 session <session> level 3 seq 1
  ping reply from s1n2-i3-12 [target s1n2-i3-12] stream s1 session <session> level 3 seq 2
"""),
    ('n1', 'ping s1n1-e4-01 s1n3-i4-23 4', 6,
"""
ping s1n1-e4-01 -> s1n3-i4-23 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-23 [target s1n3-i4-23] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-i4-34 4', 7,
"""
ping s1n1-e4-01 -> s1n4-i4-34 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-i4-34 [target s1n4-i4-34] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -o', 8,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 ObjectState [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-e4-40 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
    s1n4-e4-40 stats: data packets 0 octets 0 OAM recv 3 sent 0
    Object pef4 type seqrec
        recovery_algorithm vector, reset_timer 2000ms
        use_init_flag false, use_reset_flag false, history_length 2
        history_content ...
        latent_error_paths 2, latent_error_resets 0, latent_errors 0
        latest_valid_sequence_number 0, passed 0, discarded 0
        number_of_resets 1
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 1', 9,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-13 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 2', 10,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-23 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-i4-34 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-i4-24 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 3', 11,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-i4-34 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-e4-40 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 4', 12,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-e4-40 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -d', 13,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-e4-40 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0 delay 0
"""),
    ('n1', 'ping s1n1-e4-01 any 4', 14,
"""
ping s1n1-e4-01 -> any stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-23 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n3-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-e4-40 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n3-i4-13 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-i4-24 [target any] stream s1 session <session> level 4 seq 0
"""),

    ('n1', 'ping s1n1-e4-01 s1n4-i4-24 4 -r', 15,
"""
ping s1n1-e4-01 -> s1n4-i4-24 stream s1 session <session> level 4 count 1 interval 1000 RecordRoute [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-i4-24 [target s1n4-i4-24] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n4-i4-24 ]
"""),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -r', 0,
"""
ping s1n1-e4-01 -> s1n4-e4-40 stream s1 session <session> level 4 count 1 interval 1000 RecordRoute [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-e4-40 [target s1n4-e4-40] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
"""),
    ('n1', 'ping s1n1-e4-01 any 4 -r', 2,
"""
ping s1n1-e4-01 -> any stream s1 session <session> level 4 count 1 interval 1000 RecordRoute [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-23 [target any] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n3-i4-23 ]
  ping reply from s1n3-i4-34 [target any] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 ]
  ping reply from s1n4-i4-34 [target any] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 ]
  ping reply from s1n4-e4-40 [target any] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
  ping reply from s1n3-i4-13 [target any] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n3-i4-13 ]
  ping reply from s1n4-i4-24 [target any] stream s1 session <session> level 4 seq 0
    Record Route: [ s1n1-e4-01 s1n4-i4-24 ]
"""),
    ('n1', 'rlist s1n1-e4-01 any 4', 3,
"""
rlist s1n1-e4-01 -> any stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
Rlist result from s1n3-i4-23:
  s1n3-i4-13
  s1n3-i4-23
  s1n3-i4-34
Rlist result from s1n3-i4-34:
  s1n3-i4-13
  s1n3-i4-23
  s1n3-i4-34
Rlist result from s1n4-i4-34:
  s1n4-i4-24
  s1n4-i4-34
Rlist result from s1n4-e4-40:
  s1n4-i4-24
  s1n4-i4-34
Rlist result from s1n3-i4-13:
  s1n3-i4-13
  s1n3-i4-23
  s1n3-i4-34
Rlist result from s1n4-i4-24:
  s1n4-i4-24
  s1n4-i4-34
"""),
    ('n1', 'rlist s1n1-e4-01 any 3', 4,
"""
rlist s1n1-e4-01 -> any stream s1 session <session> level 3 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
Rlist result from s1n2-i3-12:
  s1n2-i3-12
Rlist result from s1n3-e3-23:
  s1n3-i4-13
  s1n3-i4-23
  s1n3-i4-34
Rlist result from s1n4-e3-24:
  s1n4-i4-24
  s1n4-i4-34
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-13 any 4', 5,
"""
rping s1n1-e4-01 -> s1n3-i4-13 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-e4-40 [target any] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 any 4', 6,
"""
rping s1n1-e4-01 -> s1n3-i4-13 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-e4-40 [target any] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-13 any 4', 7,
"""
rping s1n1-e4-01 -> s1n3-i4-34 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n3-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-e4-40 [target any] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-34 any 4', 8,
"""
rping s1n1-e4-01 -> s1n3-i4-34 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s1n4-i4-34 [target any] stream s1 session <session> level 4 seq 0
  ping reply from s1n4-e4-40 [target any] stream s1 session <session> level 4 seq 0
"""),
    ('n1', 'rping nonexistentmp s1n3-i4-13 4 s1n3-i4-34 any 4', 0,
"""
Error: rping command is invalid: rping start 'nonexistentmp' invalid
"""),
    ('n4', 'ping s2n4-e5-04 s2n1-i5-21 5', 2,
"""
ping s2n4-e5-04 -> s2n1-i5-21 stream s2 session <session> level 5 count 1 interval 1000 [reply to ip 10.0.0.4 port 6634]
  ping reply from s2n1-i5-21 [target s2n1-i5-21] stream s2 session <session> level 5 seq 0
"""),
    ('n4', 'ping s2n4-e5-04 s2n1-i5-31 5', 3,
"""
ping s2n4-e5-04 -> s2n1-i5-31 stream s2 session <session> level 5 count 1 interval 1000 [reply to ip 10.0.0.4 port 6634]
  ping reply from s2n1-i5-31 [target s2n1-i5-31] stream s2 session <session> level 5 seq 0
"""),
    ('n4', 'ping s2n4-e5-04 s2n1-e5-10 5', 4,
"""
ping s2n4-e5-04 -> s2n1-e5-10 stream s2 session <session> level 5 count 1 interval 1000 [reply to ip 10.0.0.4 port 6634]
  ping reply from s2n1-e5-10 [target s2n1-e5-10] stream s2 session <session> level 5 seq 0
"""),
    ('n1', 'ping s3n1-e4-01 any 4', 2,
"""
ping s3n1-e4-01 -> any stream s3 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
  ping reply from s3n3-i4-13 [target any] stream s3 session <session> level 4 seq 0
  ping reply from s3n4-i4-34 [target any] stream s3 session <session> level 4 seq 0
  ping reply from s3n4-e4-40 [target any] stream s3 session <session> level 4 seq 0
  ping reply from s3n4-i4-24 [target any] stream s3 session <session> level 4 seq 0
"""),
    ('n3', 'ping s3n3-e1-32 any 1', 1,
"""
ping s3n3-e1-32 -> any stream tx332 session <session> level 1 count 1 interval 1000 [reply to ip 10.0.0.3 port 6634]
  ping reply from s3n4-e1-24 [target any] stream tx332 session <session> level 1 seq 0
"""),
    ('n1', 'rping s1n1-e4-01 nonexistentmp 4 s1n3-i4-34 any 4', 9,
"""
rping s1n1-e4-01 -> nonexistentmp stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 nonexistentmp 4', 10,
"""
rping s1n1-e4-01 -> s1n3-i4-13 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-13 4 nonexistentmp any 4', 11,
"""
rping s1n1-e4-01 -> s1n3-i4-13 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
Rping error from s1n3-i4-13 : could not create ping request: ping start 'nonexistentmp' invalid
"""),
    ('n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 any 4 -b', 12,
"""
rping s1n1-e4-01 -> s1n3-i4-13 stream s1 session <session> level 4 count 1 interval 1000 [reply to ip 10.0.0.1 port 6634]
Rping error from s1n3-i4-13 : infinite ping count is not allowed
"""),
    ('n1', 'sessions', 0, # note: exiting telnet clears the associated sessions
"""
Stream s1 sessions:
    1 ping s1n1-e4-01 -> s1n4-e3-24 level 3 connection <background> sent 0 recv 0
Stream s3 sessions:
    1 ping s3n1-e4-01 -> s3n4-e4-40 level 4 connection <background> sent 0 recv 0
"""),
    ('n1', 'sessions s3', 0, # note: exiting telnet clears the associated sessions
"""
Stream s3 sessions:
    1 ping s3n1-e4-01 -> s3n4-e4-40 level 4 connection <background> sent 0 recv 0
"""),
    ('n1', 'ping s3n1-e4-01 s3n4-i4-34 4 -b', 3,
"""
ping s3n1-e4-01 -> s3n4-i4-34 stream s3 session <session> level 4 count 0 interval 1000 Background [reply to ip 10.0.0.1 port 6634]
"""),
    ('n1', 'sessions s3', 0, # note: exiting telnet clears the associated sessions
"""
Stream s3 sessions:
    1 ping s3n1-e4-01 -> s3n4-e4-40 level 4 connection <background> sent 0 recv 0
    3 ping s3n1-e4-01 -> s3n4-i4-34 level 4 connection <background> sent 0 recv 0
"""),
]

#TODO also check that they have the correct address
def auto_mip_test():
    verdict = True
    print("Test PseudoWire OAM automatic MIP configuration", end=" ")
    exec_bg("../r2dtwo oam/autconfig/auto.ini -v ALL:NONE")
    time.sleep(1)
    expected_list = """Available MEP Start points:
o_Eeditafter2_L5_pre-EforEditAfter in Eeditafter2 type MIP level 5 PseudoWire (pipe Eeditafter2 idx 3)
o_Eeditafter3_L5_pre-EforEditAfter in Eeditafter3 type MIP level 5 PseudoWire (pipe Eeditafter3 idx 3)
o_Eeditafter_L5_post-EforEditAfter in Eeditafter type MIP level 5 PseudoWire (pipe Eeditafter3 idx 6)
o_Eeditbefore2_L4_pre-EforEditBefore in Eeditbefore2 type MIP level 4 PseudoWire (pipe Eeditbefore2 idx 4)
o_Eeditbefore3_L4_pre-EforEditBefore in Eeditbefore3 type MIP level 4 PseudoWire (pipe Eeditbefore3 idx 4)
o_Eeditbefore_L4_post-EforEditBefore in Eeditbefore type MIP level 4 PseudoWire (pipe Eeditbefore3 idx 7)
o_Ematch2_L3_pre-EforMatch in Ematch2 type MIP level 3 PseudoWire (pipe Ematch2 idx 4)
o_Ematch3_L3_pre-EforMatch in Ematch3 type MIP level 3 PseudoWire (pipe Ematch3 idx 4)
o_Ematch_L3_post-EforMatch in Ematch type MIP level 3 PseudoWire (pipe Ematch3 idx 7)
o_Enoaddr2_L6_pre-EforNoAddr in Enoaddr2 type MIP level 6 PseudoWire (pipe Enoaddr2 idx 3) CAN'T SEND
o_Enoaddr3_L6_pre-EforNoAddr in Enoaddr3 type MIP level 6 PseudoWire (pipe Enoaddr3 idx 3) CAN'T SEND
o_Enoaddr_L6_post-EforNoAddr in Enoaddr type MIP level 6 PseudoWire (pipe Enoaddr3 idx 6) CAN'T SEND
o_Reditafter1_L5_post-RforEditAfter in Reditafter1 type MIP level 5 PseudoWire (pipe Reditafter1 idx 1)
o_Reditafter2_L5_post-RforEditAfter in Reditafter2 type MIP level 5 PseudoWire (pipe Reditafter2 idx 1)
o_Reditafter_L5_pre-RforEditAfter in Reditafter type MIP level 5 PseudoWire (pipe Reditafter idx 4)
o_Reditbefore1_L4_post-RforEditBefore in Reditbefore1 type MIP level 4 PseudoWire (pipe Reditbefore1 idx 1)
o_Reditbefore2_L4_post-RforEditBefore in Reditbefore2 type MIP level 4 PseudoWire (pipe Reditbefore2 idx 1)
o_Reditbefore_L4_pre-RforEditBefore in Reditbefore type MIP level 4 PseudoWire (pipe Reditbefore idx 5)
o_Rmatch1_L3_post-RforMatch in Rmatch1 type MIP level 3 PseudoWire (pipe Rmatch1 idx 1)
o_Rmatch2_L3_post-RforMatch in Rmatch2 type MIP level 3 PseudoWire (pipe Rmatch2 idx 1)
o_Rmatch_L3_pre-RforMatch in Rmatch type MIP level 3 PseudoWire (pipe Rmatch idx 4)
o_Rnoaddr1_L6_post-RforNoAddr in Rnoaddr1 type MIP level 6 PseudoWire (pipe Rnoaddr1 idx 1) CAN'T SEND
o_Rnoaddr2_L6_post-RforNoAddr in Rnoaddr2 type MIP level 6 PseudoWire (pipe Rnoaddr2 idx 1) CAN'T SEND
o_Rnoaddr_L6_pre-RforNoAddr in Rnoaddr type MIP level 6 PseudoWire (pipe Rnoaddr idx 4) CAN'T SEND
"""
    expected_mask = """
mask state for SequenceRecovery 'EforEditAfter'
  latent error paths 2 / 2
    o_Eeditafter3_L5_pre-EforEditAfter is not masked
    o_Eeditafter2_L5_pre-EforEditAfter is not masked
mask state for Replicate 'RforEditAfter'
  pipeline 'Reditafter1' is not masked
  pipeline 'Reditafter2' is masked, o_Reditafter2_L5_post-RforEditAfter sending mask signal
mask state for Replicate 'RforMatch'
  pipeline 'Rmatch1' is not masked
  pipeline 'Rmatch2' is not masked
mask state for SequenceRecovery 'EforMatch'
  latent error paths 2 / 2
    o_Ematch3_L3_pre-EforMatch is not masked
    o_Ematch2_L3_pre-EforMatch is not masked
mask state for SequenceRecovery 'EforNoAddr'
  latent error paths 2 / 2
    o_Enoaddr2_L6_pre-EforNoAddr is not masked
    o_Enoaddr3_L6_pre-EforNoAddr is not masked
mask state for Replicate 'RforEditBefore'
  pipeline 'Reditbefore1' is masked, o_Reditbefore1_L4_post-RforEditBefore sending mask signal
  pipeline 'Reditbefore2' is not masked
mask state for Replicate 'RforNoAddr'
  pipeline 'Rnoaddr1' is not masked
  pipeline 'Rnoaddr2' is not masked
mask state for SequenceRecovery 'EforEditBefore'
  latent error paths 2 / 2
    o_Eeditbefore2_L4_pre-EforEditBefore is not masked
    o_Eeditbefore3_L4_pre-EforEditBefore is not masked
"""
    try:
        with Telnet("127.0.0.1", 8000) as cli:
            _ = cli.recv()
            cli.send("list")
            time.sleep(0.5)
            reply = cli.recv(timeout = 2.0, aggregate=True)
            if reply.strip() != expected_list.strip():
                print(f"\nActual list:\n{reply}\nExpected list:\n{expected_list}\n")
                verdict = False

            cli.send("mask")
            time.sleep(0.5)
            reply = cli.recv(timeout = 2.0, aggregate=True)
            if reply.strip() != expected_mask.strip():
                print(f"\nActual mask:\n{reply}\nExpected mask:\n{expected_mask}\n")
                verdict = False

            cli.close()

            if verdict == True:
                print("✔")
                return True
            else:
                print("✘")
                return False
    except Exception:
        print("✘ Exception")
        return False

def run_tests(net, test):
    raddrs = {
        'n1' : "10.0.0.1",
        'n2' : "10.0.0.2",
        'n3' : "10.0.0.3",
        'n4' : "10.0.0.4",
    }
    # Cleanup & start r2dtwos & wait for init
    exec_fg("killall xterm")
    exec_fg("killall r2dtwo")
    time.sleep(0.3)
    start_r2dtwos(net, False)
    time.sleep(1.5)
    success = 0
    for node, msg, session, expected_reply in test:
        switch_netns(node)

        with Telnet(raddrs[node], 8000) as cli:
            _ = cli.recv() # OAM ready
            cli.send(msg)
            print(f"Node: {node}, command: {msg}", end=" ")
            if "any" in msg:
                reply = cli.recv(1.0, aggregate=True)
            else:
                reply = cli.recv()
            expected_reply = re.sub(r'<session>', str(session), expected_reply)
            # these numbers are unstable due to the background pings
            reply = re.sub(r'latest_valid_sequence_number \d+, passed \d+, discarded \d+',
                   r'latest_valid_sequence_number 0, passed 0, discarded 0',
                   reply)
            reply = re.sub(r'delay \d+\.\d+',
                   r'delay 0',
                   reply)
            reply = re.sub(r'data packets \d+ octets \d+',
                    r'data packets 0 octets 0',
                    reply)
            reply = re.sub(r'sent \d+ recv \d+',
                    r'sent 0 recv 0',
                    reply)
            # the reset counter can be 0 or 1 depending on startup speed
            reply = re.sub(r'number_of_resets \d', r'number_of_resets 1', reply)
            if reply.strip() == expected_reply.strip():
                success += 1
                print("✔")
            else:
                print("✘ FAILED: OAM reply different")
                print(f"Actual reply:\n{reply}\nExpected reply:\n{expected_reply}\n")

    switch_netns()
    if auto_mip_test():
        success += 1
    print(f"Successful tests: {success}/{len(test) + 1}") # +1 is the auto MIP test
    if success == len(test) + 1: # +1 the auto MIP test
        return True
    return False

def main():
    all_ok = False
    try:
        debug = False
        if len(sys.argv) >= 2 and sys.argv[1] == "debug":
            debug = True
        if debug:
            print("R2DTWO PseudoWire OAM debug")
        else:
            print("R2DTWO PseudoWire OAM test")
        net = create_net()
        config_net(net)
        if debug:
            inp = input("Do you want to start R2DWOs? (yes/no): ")
            if inp.lower() in ["yes", "y"]:
                start_r2dtwos(net, False)
            CLI(net)
        else:
            all_ok = run_tests(net, testcases)
    except Exception as ex:
        print(type(ex))
    finally:
        print("Cleanup...")
        exec_fg("killall r2dtwo")
        exec_fg("killall xterm")
        #exec_fg("killall gdb")
        net.stop()
        if all_ok:
            exit(0)
        exit(1)

if __name__ == "__main__":
    main()
