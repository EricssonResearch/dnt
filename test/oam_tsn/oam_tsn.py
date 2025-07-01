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
    talker = net.addHost('talker', ip='192.168.1.1/24')
    listener =  net.addHost('listener', ip='192.168.1.2/24')
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
    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")
    # l.cmd("ip a a 192.168.1.2/24 dev eth0")
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
    n1.cmd("tc qdisc add dev eth13 root netem delay 20ms")
    n1.cmd("tc qdisc add dev eth12 root netem delay 1ms")
    n2.cmd("tc qdisc add dev eth24 root netem delay 20ms")
    n2.cmd("tc qdisc add dev eth23 root netem delay 2ms")
    n2.cmd("tc qdisc add dev eth21 root netem delay 2ms")
    n3.cmd("tc qdisc add dev eth31 root netem delay 3ms")
    n4.cmd("tc qdisc add dev eth43 root netem delay 4ms")
    # n3.cmd("tc qdisc add dev eth34 root netem delay 10ms")

def start_r2dtwos(net, debug):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo oam/singlestage/{n}.cfg")
        else:
            # node.popen(f"xterm -T {n} -e env -i gdb -nx -ex=r --args ../r2dtwo oam/singlestage/{n}.cfg -vALL:NONE")
            node.popen(f"../r2dtwo oam/singlestage/{n}.cfg -vALL:NONE")

# list of (sender, message, [expected JSON replies])
# The sender 'node' sending 'message' from telnet and expect the list of replies
testcases = [
    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3',
"""
OAM request ping session 2 seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:2 seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3 -n 3',
"""
OAM request ping session 3 seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 3 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:3 seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),


    ('n1', 'ping s1n1-e4-01 s1n3-i4-23 4',
"""
OAM request ping session 4 seq 0, s1n1-e4-01 -> s1n3-i4-23 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target s1n3-i4-23; reply from s1n3-i4-23
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-i4-34 4',
"""
OAM request ping session 5 seq 0, s1n1-e4-01 -> s1n4-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:5 seq 0 lvl 4 R - ping on stream s1 target s1n4-i4-34; reply from s1n4-i4-34
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -o',
"""
OAM request ping session 6 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: yes	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:6 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	Object pef4 type seqrec
		recovery_algorithm vector, reset_timer 2000ms
		use_init_flag false, use_reset_flag false, history_length 2
		history_content ...
		latent_error_paths 2, latent_error_resets 0, latent_errors 0
		latest_valid_sequence_number 0, passed 0, discarded 0
		number_of_resets 0
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 1',
"""
OAM request ping session 7 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:7 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n3-i4-13
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 2',
"""
OAM request ping session 8 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:8 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n3-i4-23
  oam_r s1:8 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-i4-34
  oam_r s1:8 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-i4-24
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 3',
"""
OAM request ping session 9 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:9 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-i4-34
  oam_r s1:9 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 4',
"""
OAM request ping session 10 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:10 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -d',
"""
OAM request ping session 11 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:11 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40 delay 0
"""
     ),

    ('n1', 'ping s1n1-e4-01 any 4',
"""
OAM request ping session 12 seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:12 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-23
  oam_r s1:12 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:12 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:12 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
  oam_r s1:12 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-13
  oam_r s1:12 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-24
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-i4-24 4 -r',
"""
OAM request ping session 13 seq 0, s1n1-e4-01 -> s1n4-i4-24 level 4 count 1 interval 1000, rr: yes os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target s1n4-i4-24; reply from s1n4-i4-24
	Record Route: [ s1n1-e4-01 s1n4-i4-24 ]
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -r',
"""
OAM request ping session 14 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: yes os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:14 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
"""
    ),

    (
        'n1', 'rlist s1n1-e4-01 any 4',
"""
OAM request rlist session 15 seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
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
"""
    ),

    (
        'n1', 'rlist s1n1-e4-01 any 3',
"""
OAM request rlist session 0 seq 0, s1n1-e4-01 -> any level 3 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
Rlist result from s1n2-i3-12:
s1n2-i3-12

Rlist result from s1n3-e3-23:
s1n3-i4-13
s1n3-i4-23
s1n3-i4-34

Rlist result from s1n4-e3-24:
s1n4-i4-24
s1n4-i4-34
"""
    ),
    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-13 any 4',
"""
OAM request rping session 2 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:2 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:2 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:2 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 any 4',
"""
OAM request rping session 3 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:3 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:3 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-13 any 4',
"""
OAM request rping session 4 seq 0, s1n1-e4-01 -> s1n3-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-34 any 4',
"""
OAM request rping session 5 seq 0, s1n1-e4-01 -> s1n3-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s1:5 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:5 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping nonexistentmp s1n3-i4-13 4 s1n3-i4-34 any 4',
"""
Error: rping command is invalid: rping start 'nonexistentmp' invalid
"""
    ),

    (
        'n4', 'ping s2n4-e5-04 s2n1-i5-21 5',
"""
OAM request ping session 2 seq 0, s2n4-e5-04 -> s2n1-i5-21 level 5 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.4 port 6634]
  oam_r s2:2 seq 0 lvl 5 R - ping on stream s2 target s2n1-i5-21; reply from s2n1-i5-21
"""
    ),
    (
        'n4', 'ping s2n4-e5-04 s2n1-i5-31 5',
"""
OAM request ping session 3 seq 0, s2n4-e5-04 -> s2n1-i5-31 level 5 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.4 port 6634]
  oam_r s2:3 seq 0 lvl 5 R - ping on stream s2 target s2n1-i5-31; reply from s2n1-i5-31
"""
    ),
    (
        'n4', 'ping s2n4-e5-04 s2n1-e5-10 5',
"""
OAM request ping session 4 seq 0, s2n4-e5-04 -> s2n1-e5-10 level 5 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.4 port 6634]
  oam_r s2:4 seq 0 lvl 5 R - ping on stream s2 target s2n1-e5-10; reply from s2n1-e5-10
"""
    ),

    (
        'n1', 'ping s3n1-e4-01 any 4',
"""
OAM request ping session 2 seq 0, s3n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n3-i4-13
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-i4-34
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-e4-40
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-i4-24
"""
    ),
    (
        'n3', 'ping s3n3-e1-32 any 1',
"""
OAM request ping session 1 seq 0, s3n3-e1-32 -> any level 1 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.3 port 6634]
  oam_r tx332:1 seq 0 lvl 1 R - ping on stream tx332 target any; reply from s3n4-e1-24
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 nonexistentmp 4 s1n3-i4-34 any 4',
"""
OAM request rping session 6 seq 0, s1n1-e4-01 -> nonexistentmp level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634] 
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 nonexistentmp 4',
"""
OAM request rping session 7 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634] 
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 nonexistentmp any 4',
"""
OAM request rping session 8 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip 10.0.0.1 port 6634]
Rping error from s1n3-i4-13 : could not create ping request: ping start 'nonexistentmp' invalid
"""
    ),
]

def auto_mip_test():
    print("Test OAM automatic MIP configuration", end=" ")
    exec_bg("../r2dtwo oam/autconfig/auto.ini -v ALL:NONE")
    time.sleep(1)
    expected_reply = """Available MEP Start points:
o_C1_L2_post-E1 in C1 type MIP level 2 PseudoWire (pipe M1 idx 6)
o_C1_L3_pre-E2 in C1 type MIP level 3 PseudoWire (pipe M1 idx 8)
o_C2_L3_pre-E2 in C2 type MIP level 3 PseudoWire (pipe C2 idx 3)
o_C_L3_post-E2 in C type MIP level 3 PseudoWire (pipe M1 idx 11)
o_C_L4_pre-R1 in C type MIP level 4 PseudoWire (pipe M1 idx 13)
o_M1_L2_pre-E1 in M1 type MIP level 2 PseudoWire (pipe M1 idx 3)
o_M2_L2_pre-E1 in M2 type MIP level 2 PseudoWire (pipe M2 idx 3)
o_M5_L4_post-R1 in M5 type MIP level 4 PseudoWire (pipe M5 idx 1)
o_M5_L5_pre-R2 in M5 type MIP level 5 PseudoWire (pipe M5 idx 3)
o_M6_L4_post-R1 in M6 type MIP level 4 PseudoWire (pipe M6 idx 1)
o_M7_L5_post-R2 in M7 type MIP level 5 PseudoWire (pipe M7 idx 1)
o_M8_L5_post-R2 in M8 type MIP level 5 PseudoWire (pipe M8 idx 1)
"""
    try:
        with Telnet("127.0.0.1", 8000) as cli:
            _ = cli.recv()
            cli.send("list")
            time.sleep(0.5)
            reply = cli.recv(timeout = 2.0, aggregate=True)
            cli.close()
            if reply == expected_reply:
                print("✔")
                return True
            else:
                print("✘ ")
                print(f"Actual reply:\n{reply}\nExpected reply:\n{expected_reply}\n")
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
    for node, msg, expected_reply in test:
        switch_netns(node)

        with Telnet(raddrs[node], 8000) as cli:
            _ = cli.recv() # OAM ready
            cli.send(msg)
            print(f"Node: {node}, command: {msg}", end=" ")
            if "any" in msg:
                reply = cli.recv(1.0, aggregate=True)
            else:
                reply = cli.recv()
            # these numbers are unstable due to the background pings
            reply = re.sub(r'latest_valid_sequence_number \d+, passed \d+, discarded \d+',
                   r'latest_valid_sequence_number 0, passed 0, discarded 0',
                   reply)
            reply = re.sub(r'delay \d+\.\d+',
                   r'delay 0',
                   reply)      
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
            print("R2DTWO OAM debug")
        else:
            print("R2DTWO OAM test")
        net = create_net()
        config_net(net)
        if debug:
            inp = input("Do you want ti start R2DWOs? (yes/no): ")
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
