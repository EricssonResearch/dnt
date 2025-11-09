#!/usr/bin/python3

from mininet.net import Mininet
#from mininet.nodelib import LinuxBridge
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

    management switch: s1
        connecten to n1, n2, n3, n4 (eth_m interface)


    Router-local IPs for OAM: 10.0.0.{1,2,3,4}/32
    """
    try:
        net = Mininet( autoStaticArp=True, topo=None,  build=False )

        # nodes: a, b, c, d, talker, listener
        talker   = net.addHost('talker', ip='192.168.1.1/24')
        listener = net.addHost('listener', ip='192.168.1.2/24')
        n1 = net.addHost('n1', ip=None)
        n2 = net.addHost('n2', ip=None)
        n3 = net.addHost('n3', ip=None)
        n4 = net.addHost('n4', ip=None)

        # switch
        s1 = net.addHost('s1')

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

        net.addLink(n1, s1,intfName1='eth_m', intfName2='eth_n1' )
        net.addLink(n2, s1,intfName1='eth_m', intfName2='eth_n2' )
        net.addLink(n3, s1,intfName1='eth_m', intfName2='eth_n3' )
        net.addLink(n4, s1,intfName1='eth_m', intfName2='eth_n4' )

        net.build()

    except Exception as ex:
        print(type(ex), ex)
    return net

def config_net(net):
    t, l, n1, n2, n3, n4, s1 = [net.get(n) for n in ["talker", "listener", "n1", "n2", "n3", "n4", "s1"]]
    ip_lo = 1
    for n in [n1, n2, n3, n4]:
        n.cmd(f"ip a a 10.0.0.{ip_lo}/32 dev lo")
        ip_lo += 1
    # t.cmd("ip a a 192.168.1.1/24 dev eth0")
    # l.cmd("ip a a 192.168.1.2/24 dev eth0")
    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")

    # delay
    n1.cmd("tc qdisc add dev eth13 root netem delay 20ms")
    n1.cmd("tc qdisc add dev eth12 root netem delay 1ms")
    n2.cmd("tc qdisc add dev eth24 root netem delay 20ms")
    n2.cmd("tc qdisc add dev eth23 root netem delay 2ms")
    n2.cmd("tc qdisc add dev eth21 root netem delay 2ms")
    n3.cmd("tc qdisc add dev eth31 root netem delay 3ms")
    n4.cmd("tc qdisc add dev eth43 root netem delay 4ms")
    # n3.cmd("tc qdisc add dev eth34 root netem delay 10ms")

    # Create the Linux bridge
    s1.cmd('ip link add s1 type bridge')
    s1.cmd('ip link set s1 up')

    # Attach the ports
    s1.cmd('ip link set eth_n1 master s1')
    s1.cmd('ip link set eth_n2 master s1')
    s1.cmd('ip link set eth_n3 master s1')
    s1.cmd('ip link set eth_n4 master s1')

def start_r2dtwos(net, debug):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo oam_tsn/singlestage/{n}.cfg")
        else:
            # node.popen(f"xterm -T {n} -e env -i gdb -nx -ex=r --args ../r2dtwo oam/singlestage/{n}.cfg -vALL:NONE")
            node.popen(f"../r2dtwo oam_tsn/singlestage/{n}.cfg -vALL:NONE")

# list of (sender, message, [expected JSON replies])
# The sender 'node' sending 'message' from telnet and expect the list of replies
testcases = [
    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3', 2,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),

    ('n1', 'ping@oam1 s1n1-e4-01 s1n2-i3-12 3', 3,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),


    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3 -n 3', 4,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 3 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),


    ('n1', 'ping s1n1-e4-01 s1n3-i4-23 4', 5,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-23 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n3-i4-23; reply from s1n3-i4-23
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-i4-34 4', 6,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n4-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n4-i4-34; reply from s1n4-i4-34
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -o', 7,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: yes	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	s1n4-e4-40 stats: data packets 0 octets 0 OAM recv 3 sent 0
	Object pef4 type seqrec
		recovery_algorithm vector, reset_timer 2000ms
		use_init_flag false, use_reset_flag false, history_length 2
		history_content ...
		latent_error_paths 2, latent_error_resets 0, latent_errors 0
		latest_valid_sequence_number 0, passed 0, discarded 0
		number_of_resets 1
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -d', 8,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40 delay 0
"""
     ),

    ('n1', 'ping s1n1-e4-01 any 4', 9,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-23
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-13
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-24
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-i4-24 4 -r', 10,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n4-i4-24 level 4 count 1 interval 1000, rr: yes os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n4-i4-24; reply from s1n4-i4-24
	Record Route: [ s1n1-e4-01 s1n4-i4-24 ]
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -r', 11,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: yes os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
"""
    ),

    ('n1', 'ping@oam1 s1n1-e4-01 s1n4-e4-40 4 -r', 12,
"""
OAM request ping session <session> seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: yes os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
"""
    ),

    ('n1', 'ping s1n1-e4-01 any 4 -r', 13,
"""
OAM request ping session 13 seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: yes os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-23
	Record Route: [ s1n1-e4-01 s1n3-i4-23 ]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 ]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 ]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-13
	Record Route: [ s1n1-e4-01 s1n3-i4-13 ]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-24
	Record Route: [ s1n1-e4-01 s1n4-i4-24 ]
"""),

    (
        'n1', 'rlist s1n1-e4-01 any 4', 14,
"""
OAM request rlist session <session> seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
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
        'n1', 'rlist s1n1-e4-01 any 3', 15,
"""
OAM request rlist session <session> seq 0, s1n1-e4-01 -> any level 3 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
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
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-13 any 4', 0,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 any 4', 2,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-13 any 4', 3,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-34 any 4', 4,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:<session> seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping nonexistentmp s1n3-i4-13 4 s1n3-i4-34 any 4', 0,
"""
Error: rping command is invalid: rping start 'nonexistentmp' invalid
"""
    ),

    (
        'n4', 'ping s2n4-e5-04 s2n1-i5-21 5', 2,
"""
OAM request ping session <session> seq 0, s2n4-e5-04 -> s2n1-i5-21 level 5 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s2:<session> seq 0 lvl 5 R - ping on stream s2 target s2n1-i5-21; reply from s2n1-i5-21
"""
    ),
    (
        'n4', 'ping s2n4-e5-04 s2n1-i5-31 5', 3,
"""
OAM request ping session <session> seq 0, s2n4-e5-04 -> s2n1-i5-31 level 5 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s2:<session> seq 0 lvl 5 R - ping on stream s2 target s2n1-i5-31; reply from s2n1-i5-31
"""
    ),
    (
        'n4', 'ping s2n4-e5-04 s2n1-e5-10 5', 4,
"""
OAM request ping session <session> seq 0, s2n4-e5-04 -> s2n1-e5-10 level 5 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s2:<session> seq 0 lvl 5 R - ping on stream s2 target s2n1-e5-10; reply from s2n1-e5-10
"""
    ),

    (
        'n1', 'ping s3n1-e4-01 any 4', 2,
"""
OAM request ping session <session> seq 0, s3n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r s3:<session> seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n3-i4-13
  oam_r s3:<session> seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-i4-34
  oam_r s3:<session> seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-e4-40
  oam_r s3:<session> seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-i4-24
"""
    ),
    (
        'n3', 'ping s3n3-e1-32 any 1', 1,
"""
OAM request ping session <session> seq 0, s3n3-e1-32 -> any level 1 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
  oam_r tx332:<session> seq 0 lvl 1 R - ping on stream tx332 target any; reply from s3n4-e1-24
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 nonexistentmp 4 s1n3-i4-34 any 4', 5,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> nonexistentmp level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 nonexistentmp 4', 6,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 nonexistentmp any 4', 7,
"""
OAM request rping session <session> seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to mac 00:00:00:00:00:00]
Rping error from s1n3-i4-13 : could not create ping request: ping start 'nonexistentmp' invalid
"""
    ),
]

#TODO also check that they have the correct address
def auto_mip_test():
    verdict = True
    print("Test TSN OAM automatic MIP configuration", end=" ")
    exec_bg("../r2dtwo oam_tsn/autconfig/auto.ini -v ALL:NONE")
    time.sleep(1)
    expected_list = """Available MEP Start points:
o_Eafter_after2_L4_pre-Eaa in Eafter_after2 type MIP level 4 TSN (pipe Eafter_after2 idx 2)
o_Eafter_after3_L4_pre-Eaa in Eafter_after3 type MIP level 4 TSN (pipe Eafter_after3 idx 2)
o_Eafter_after_L4_post-Eaa in Eafter_after type MIP level 4 TSN (pipe Eafter_after2 idx 5)
o_Eafter_before2_L4_pre-Eab in Eafter_before2 type MIP level 4 TSN (pipe Eafter_before2 idx 3)
o_Eafter_before3_L4_pre-Eab in Eafter_before3 type MIP level 4 TSN (pipe Eafter_before3 idx 3)
o_Eafter_before_L4_post-Eab in Eafter_before type MIP level 4 TSN (pipe Eafter_before2 idx 6)
o_Eafter_match2_L4_pre-Eam in Eafter_match2 type MIP level 4 TSN (pipe Eafter_match2 idx 2)
o_Eafter_match3_L4_pre-Eam in Eafter_match3 type MIP level 4 TSN (pipe Eafter_match3 idx 2)
o_Eafter_match_L4_post-Eam in Eafter_match type MIP level 4 TSN (pipe Eafter_match2 idx 5)
o_Eafter_missing2_L4_pre-Eax in Eafter_missing2 type MIP level 4 TSN (pipe Eafter_missing2 idx 2) CAN'T SEND
o_Eafter_missing3_L4_pre-Eax in Eafter_missing3 type MIP level 4 TSN (pipe Eafter_missing3 idx 2) CAN'T SEND
o_Eafter_missing_L4_post-Eax in Eafter_missing type MIP level 4 TSN (pipe Eafter_missing2 idx 5) CAN'T SEND
o_Ebefore_after2_L4_pre-Eba in Ebefore_after2 type MIP level 4 TSN (pipe Ebefore_after2 idx 3)
o_Ebefore_after3_L4_pre-Eba in Ebefore_after3 type MIP level 4 TSN (pipe Ebefore_after3 idx 3)
o_Ebefore_after_L4_post-Eba in Ebefore_after type MIP level 4 TSN (pipe Ebefore_after2 idx 6)
o_Ebefore_before2_L4_pre-Ebb in Ebefore_before2 type MIP level 4 TSN (pipe Ebefore_before2 idx 3)
o_Ebefore_before3_L4_pre-Ebb in Ebefore_before3 type MIP level 4 TSN (pipe Ebefore_before3 idx 3)
o_Ebefore_before_L4_post-Ebb in Ebefore_before type MIP level 4 TSN (pipe Ebefore_before2 idx 6)
o_Ebefore_match2_L4_pre-Ebm in Ebefore_match2 type MIP level 4 TSN (pipe Ebefore_match2 idx 3)
o_Ebefore_match3_L4_pre-Ebm in Ebefore_match3 type MIP level 4 TSN (pipe Ebefore_match3 idx 3)
o_Ebefore_match_L4_post-Ebm in Ebefore_match type MIP level 4 TSN (pipe Ebefore_match2 idx 6)
o_Ebefore_missing2_L4_pre-Ebx in Ebefore_missing2 type MIP level 4 TSN (pipe Ebefore_missing2 idx 3) CAN'T SEND
o_Ebefore_missing3_L4_pre-Ebx in Ebefore_missing3 type MIP level 4 TSN (pipe Ebefore_missing3 idx 3) CAN'T SEND
o_Ebefore_missing_L4_post-Ebx in Ebefore_missing type MIP level 4 TSN (pipe Ebefore_missing2 idx 6) CAN'T SEND
o_Ematch_after2_L4_pre-Ema in Ematch_after2 type MIP level 4 TSN (pipe Ematch_after2 idx 2)
o_Ematch_after3_L4_pre-Ema in Ematch_after3 type MIP level 4 TSN (pipe Ematch_after3 idx 2)
o_Ematch_after_L4_post-Ema in Ematch_after type MIP level 4 TSN (pipe Ematch_after2 idx 5)
o_Ematch_before2_L4_pre-Emb in Ematch_before2 type MIP level 4 TSN (pipe Ematch_before2 idx 3)
o_Ematch_before3_L4_pre-Emb in Ematch_before3 type MIP level 4 TSN (pipe Ematch_before3 idx 3)
o_Ematch_before_L4_post-Emb in Ematch_before type MIP level 4 TSN (pipe Ematch_before2 idx 6)
o_Ematch_match2_L4_pre-Emm in Ematch_match2 type MIP level 4 TSN (pipe Ematch_match2 idx 2)
o_Ematch_match3_L4_pre-Emm in Ematch_match3 type MIP level 4 TSN (pipe Ematch_match3 idx 2)
o_Ematch_match_L4_post-Emm in Ematch_match type MIP level 4 TSN (pipe Ematch_match2 idx 5)
o_Ematch_missing2_L4_pre-Ema in Ematch_missing2 type MIP level 4 TSN (pipe Ematch_missing2 idx 2) CAN'T SEND
o_Ematch_missing3_L4_pre-Ema in Ematch_missing3 type MIP level 4 TSN (pipe Ematch_missing3 idx 2) CAN'T SEND
o_Ematch_missing_L4_post-Ema in Ematch_missing type MIP level 4 TSN (pipe Ematch_missing2 idx 5) CAN'T SEND
o_Emissing_after2_L4_pre-Exa in Emissing_after2 type MIP level 4 TSN (pipe Emissing_after2 idx 2)
o_Emissing_after3_L4_pre-Exa in Emissing_after3 type MIP level 4 TSN (pipe Emissing_after3 idx 2)
o_Emissing_after_L4_post-Exa in Emissing_after type MIP level 4 TSN (pipe Emissing_after2 idx 5)
o_Emissing_before2_L4_pre-Exb in Emissing_before2 type MIP level 4 TSN (pipe Emissing_before2 idx 3)
o_Emissing_before3_L4_pre-Exb in Emissing_before3 type MIP level 4 TSN (pipe Emissing_before3 idx 3)
o_Emissing_before_L4_post-Exb in Emissing_before type MIP level 4 TSN (pipe Emissing_before2 idx 6)
o_Emissing_match2_L4_pre-Exm in Emissing_match2 type MIP level 4 TSN (pipe Emissing_match2 idx 2)
o_Emissing_match3_L4_pre-Exm in Emissing_match3 type MIP level 4 TSN (pipe Emissing_match3 idx 2)
o_Emissing_match_L4_post-Exm in Emissing_match type MIP level 4 TSN (pipe Emissing_match2 idx 5)
o_Emissing_missing2_L4_pre-Exx in Emissing_missing2 type MIP level 4 TSN (pipe Emissing_missing2 idx 2) CAN'T SEND
o_Emissing_missing3_L4_pre-Exx in Emissing_missing3 type MIP level 4 TSN (pipe Emissing_missing3 idx 2) CAN'T SEND
o_Emissing_missing_L4_post-Exx in Emissing_missing type MIP level 4 TSN (pipe Emissing_missing2 idx 5) CAN'T SEND
o_Rafter_after_L3_pre-Raa in Rafter_after type MIP level 3 TSN (pipe Rafter_after idx 2)
o_Rafter_before_L3_pre-Rab in Rafter_before type MIP level 3 TSN (pipe Rafter_before idx 3)
o_Rafter_match_L3_pre-Ram in Rafter_match type MIP level 3 TSN (pipe Rafter_match idx 2)
o_Rafter_missing_L3_pre-Rax in Rafter_missing type MIP level 3 TSN (pipe Rafter_missing idx 2) CAN'T SEND
o_Rbefore_after_L3_pre-Rba in Rbefore_after type MIP level 3 TSN (pipe Rbefore_after idx 3)
o_Rbefore_before_L3_pre-Rbb in Rbefore_before type MIP level 3 TSN (pipe Rbefore_before idx 3)
o_Rbefore_match_L3_pre-Rbm in Rbefore_match type MIP level 3 TSN (pipe Rbefore_match idx 3)
o_Rbefore_missing_L3_pre-Rbx in Rbefore_missing type MIP level 3 TSN (pipe Rbefore_missing idx 3) CAN'T SEND
o_Rmatch_after_L3_pre-Rma in Rmatch_after type MIP level 3 TSN (pipe Rmatch_after idx 2)
o_Rmatch_before_L3_pre-Rmb in Rmatch_before type MIP level 3 TSN (pipe Rmatch_before idx 3)
o_Rmatch_match_L3_pre-Rmm in Rmatch_match type MIP level 3 TSN (pipe Rmatch_match idx 2)
o_Rmatch_missing_L3_pre-Rmx in Rmatch_missing type MIP level 3 TSN (pipe Rmatch_missing idx 2) CAN'T SEND
o_Rmissing_after_L3_pre-Rxa in Rmissing_after type MIP level 3 TSN (pipe Rmissing_after idx 2)
o_Rmissing_before_L3_pre-Rxb in Rmissing_before type MIP level 3 TSN (pipe Rmissing_before idx 3)
o_Rmissing_match_L3_pre-Rxm in Rmissing_match type MIP level 3 TSN (pipe Rmissing_match idx 2)
o_Rmissing_missing_L3_pre-Rxx in Rmissing_missing type MIP level 3 TSN (pipe Rmissing_missing idx 2) CAN'T SEND
o_repl_aa1_L3_post-Raa in repl_aa1 type MIP level 3 TSN (pipe repl_aa1 idx 1)
o_repl_aa2_L3_post-Raa in repl_aa2 type MIP level 3 TSN (pipe repl_aa2 idx 1)
o_repl_ab1_L3_post-Rab in repl_ab1 type MIP level 3 TSN (pipe repl_ab1 idx 1)
o_repl_ab2_L3_post-Rab in repl_ab2 type MIP level 3 TSN (pipe repl_ab2 idx 1)
o_repl_am1_L3_post-Ram in repl_am1 type MIP level 3 TSN (pipe repl_am1 idx 1)
o_repl_am2_L3_post-Ram in repl_am2 type MIP level 3 TSN (pipe repl_am2 idx 1)
o_repl_ax1_L3_post-Rax in repl_ax1 type MIP level 3 TSN (pipe repl_ax1 idx 1) CAN'T SEND
o_repl_ax2_L3_post-Rax in repl_ax2 type MIP level 3 TSN (pipe repl_ax2 idx 1) CAN'T SEND
o_repl_ba1_L3_post-Rba in repl_ba1 type MIP level 3 TSN (pipe repl_ba1 idx 1)
o_repl_ba2_L3_post-Rba in repl_ba2 type MIP level 3 TSN (pipe repl_ba2 idx 1)
o_repl_bb1_L3_post-Rbb in repl_bb1 type MIP level 3 TSN (pipe repl_bb1 idx 1)
o_repl_bb2_L3_post-Rbb in repl_bb2 type MIP level 3 TSN (pipe repl_bb2 idx 1)
o_repl_bm1_L3_post-Rbm in repl_bm1 type MIP level 3 TSN (pipe repl_bm1 idx 1)
o_repl_bm2_L3_post-Rbm in repl_bm2 type MIP level 3 TSN (pipe repl_bm2 idx 1)
o_repl_bx1_L3_post-Rbx in repl_bx1 type MIP level 3 TSN (pipe repl_bx1 idx 1) CAN'T SEND
o_repl_bx2_L3_post-Rbx in repl_bx2 type MIP level 3 TSN (pipe repl_bx2 idx 1) CAN'T SEND
o_repl_ma1_L3_post-Rma in repl_ma1 type MIP level 3 TSN (pipe repl_ma1 idx 1)
o_repl_ma2_L3_post-Rma in repl_ma2 type MIP level 3 TSN (pipe repl_ma2 idx 1)
o_repl_mb1_L3_post-Rmb in repl_mb1 type MIP level 3 TSN (pipe repl_mb1 idx 1)
o_repl_mb2_L3_post-Rmb in repl_mb2 type MIP level 3 TSN (pipe repl_mb2 idx 1)
o_repl_mm1_L3_post-Rmm in repl_mm1 type MIP level 3 TSN (pipe repl_mm1 idx 1)
o_repl_mm2_L3_post-Rmm in repl_mm2 type MIP level 3 TSN (pipe repl_mm2 idx 1)
o_repl_mx1_L3_post-Rmx in repl_mx1 type MIP level 3 TSN (pipe repl_mx1 idx 1) CAN'T SEND
o_repl_mx2_L3_post-Rmx in repl_mx2 type MIP level 3 TSN (pipe repl_mx2 idx 1) CAN'T SEND
o_repl_xa1_L3_post-Rxa in repl_xa1 type MIP level 3 TSN (pipe repl_xa1 idx 1)
o_repl_xa2_L3_post-Rxa in repl_xa2 type MIP level 3 TSN (pipe repl_xa2 idx 1)
o_repl_xb1_L3_post-Rxb in repl_xb1 type MIP level 3 TSN (pipe repl_xb1 idx 1)
o_repl_xb2_L3_post-Rxb in repl_xb2 type MIP level 3 TSN (pipe repl_xb2 idx 1)
o_repl_xm1_L3_post-Rxm in repl_xm1 type MIP level 3 TSN (pipe repl_xm1 idx 1)
o_repl_xm2_L3_post-Rxm in repl_xm2 type MIP level 3 TSN (pipe repl_xm2 idx 1)
o_repl_xx1_L3_post-Rxx in repl_xx1 type MIP level 3 TSN (pipe repl_xx1 idx 1) CAN'T SEND
o_repl_xx2_L3_post-Rxx in repl_xx2 type MIP level 3 TSN (pipe repl_xx2 idx 1) CAN'T SEND

"""
    expected_mask = """
mask state for SequenceRecovery 'Exa'
  latent error paths 2 / 2
    o_Emissing_after3_L4_pre-Exa is not masked
    o_Emissing_after2_L4_pre-Exa is not masked
mask state for Replicate 'Rba'
  pipeline 'repl_ba1' is masked, o_repl_ba1_L3_post-Rba sending mask signal
  pipeline 'repl_ba2' is not masked
mask state for Replicate 'Raa'
  pipeline 'repl_aa1' is masked, o_repl_aa1_L3_post-Raa sending mask signal
  pipeline 'repl_aa2' is not masked
mask state for SequenceRecovery 'Eba'
  latent error paths 2 / 2
    o_Ebefore_after2_L4_pre-Eba is not masked
    o_Ebefore_after3_L4_pre-Eba is not masked
mask state for SequenceRecovery 'Eaa'
  latent error paths 2 / 2
    o_Eafter_after2_L4_pre-Eaa is not masked
    o_Eafter_after3_L4_pre-Eaa is not masked
mask state for Replicate 'Rma'
  pipeline 'repl_ma1' is not masked
  pipeline 'repl_ma2' is not masked
mask state for SequenceRecovery 'Ema'
  latent error paths 2 / 2
    o_Ematch_after3_L4_pre-Ema is not masked
    o_Ematch_missing2_L4_pre-Ema is not masked
    o_Ematch_after2_L4_pre-Ema is not masked
    o_Ematch_missing3_L4_pre-Ema is not masked
mask state for Replicate 'Rxa'
  pipeline 'repl_xa1' is not masked
  pipeline 'repl_xa2' is not masked
mask state for Replicate 'Ram'
  pipeline 'repl_am1' is not masked
  pipeline 'repl_am2' is not masked
mask state for Replicate 'Rxb'
  pipeline 'repl_xb1' is not masked
  pipeline 'repl_xb2' is not masked
mask state for SequenceRecovery 'Ebm'
  latent error paths 2 / 2
    o_Ebefore_match2_L4_pre-Ebm is not masked
    o_Ebefore_match3_L4_pre-Ebm is not masked
mask state for SequenceRecovery 'Eam'
  latent error paths 2 / 2
    o_Eafter_match2_L4_pre-Eam is not masked
    o_Eafter_match3_L4_pre-Eam is not masked
mask state for SequenceRecovery 'Exb'
  latent error paths 2 / 2
    o_Emissing_before2_L4_pre-Exb is not masked
    o_Emissing_before3_L4_pre-Exb is not masked
mask state for Replicate 'Rmm'
  pipeline 'repl_mm1' is not masked
  pipeline 'repl_mm2' is not masked
mask state for Replicate 'Rbb'
  pipeline 'repl_bb1' is not masked
  pipeline 'repl_bb2' is not masked
mask state for Replicate 'Rxx'
  pipeline 'repl_xx1' is not masked
  pipeline 'repl_xx2' is not masked
mask state for Replicate 'Rab'
  pipeline 'repl_ab1' is not masked
  pipeline 'repl_ab2' is masked, o_repl_ab2_L3_post-Rab sending mask signal
mask state for SequenceRecovery 'Emm'
  latent error paths 2 / 2
    o_Ematch_match2_L4_pre-Emm is not masked
    o_Ematch_match3_L4_pre-Emm is not masked
mask state for SequenceRecovery 'Ebb'
  latent error paths 2 / 2
    o_Ebefore_before2_L4_pre-Ebb is not masked
    o_Ebefore_before3_L4_pre-Ebb is not masked
mask state for SequenceRecovery 'Exx'
  latent error paths 2 / 2
    o_Emissing_missing2_L4_pre-Exx is not masked
    o_Emissing_missing3_L4_pre-Exx is not masked
mask state for Replicate 'Rbx'
  pipeline 'repl_bx1' is not masked
  pipeline 'repl_bx2' is not masked
mask state for SequenceRecovery 'Eab'
  latent error paths 2 / 2
    o_Eafter_before2_L4_pre-Eab is not masked
    o_Eafter_before3_L4_pre-Eab is not masked
mask state for Replicate 'Rmb'
  pipeline 'repl_mb1' is not masked
  pipeline 'repl_mb2' is not masked
mask state for Replicate 'Rax'
  pipeline 'repl_ax1' is not masked
  pipeline 'repl_ax2' is not masked
mask state for Replicate 'Rxm'
  pipeline 'repl_xm1' is not masked
  pipeline 'repl_xm2' is not masked
mask state for SequenceRecovery 'Ebx'
  latent error paths 2 / 2
    o_Ebefore_missing2_L4_pre-Ebx is not masked
    o_Ebefore_missing3_L4_pre-Ebx is not masked
mask state for SequenceRecovery 'Emb'
  latent error paths 2 / 2
    o_Ematch_before2_L4_pre-Emb is not masked
    o_Ematch_before3_L4_pre-Emb is not masked
mask state for SequenceRecovery 'Eax'
  latent error paths 2 / 2
    o_Eafter_missing2_L4_pre-Eax is not masked
    o_Eafter_missing3_L4_pre-Eax is not masked
mask state for SequenceRecovery 'Exm'
  latent error paths 2 / 2
    o_Emissing_match2_L4_pre-Exm is not masked
    o_Emissing_match3_L4_pre-Exm is not masked
mask state for Replicate 'Rmx'
  pipeline 'repl_mx1' is not masked
  pipeline 'repl_mx2' is not masked
mask state for Replicate 'Rbm'
  pipeline 'repl_bm1' is not masked
  pipeline 'repl_bm2' is masked, o_repl_bm2_L3_post-Rbm sending mask signal
"""
    try:
        with Telnet("127.0.0.1", 8000) as cli:
            _ = cli.recv()
            cli.send("list")
            time.sleep(0.5)
            reply = cli.recv(timeout = 2.0, aggregate=True)
            if reply.strip() != expected_list.strip():
                print(f"Actual list:\n{reply}\nExpected list:\n{expected_list}\n")
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
            # sometimes the reset counter is 0 (TODO when is the reset supposed to happen?)
            reply = re.sub(r'number_of_resets \d', r'number_of_resets 1', reply)
            # mac addresses are random
            reply = re.sub(r'reply to mac ([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}',
                   r'reply to mac 00:00:00:00:00:00',
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
            inp = input("Do you want to start R2DWOs? (yes/no): ")
            if inp.lower() in ["yes", "y"]:
                start_r2dtwos(net, False)
            CLI(net)
        else:
            all_ok = run_tests(net, testcases)
    except Exception as ex:
        print(type(ex), ex)
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
