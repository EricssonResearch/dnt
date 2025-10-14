#!/usr/bin/python3

from mininet.net import Mininet
from mininet.node import Host, Node
from mininet.cli import CLI
from mininet.log import setLogLevel, info
#from threading import Thread
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
    fd01:a1fa::a                    fd07:a1fa::e

    Router-local IPs for OAM: fd1{1,2,3,4}:fade:0
    """
#    net = Mininet(autoStaticArp=True, waitConnected=True)
    net = Mininet(waitConnected=True)

    info('*** Adding hosts\n')
    hostnames = ['talker', 'n1', 'n2', 'n3', 'n4', 'listener']
    talker, n1, n2, n3, n4, listener = [net.addHost(n, ip=None) for n in hostnames]

    for node in [talker, n1, n2, n3, n4, listener]:
        #node.cmd("sysctl -w net.ipv4.ip_forward=1")
        node.cmd("sysctl -w net.ipv6.conf.all.seg6_enabled=1")
        node.cmd("sysctl -w net.ipv6.conf.default.seg6_enabled=1")
        node.cmd("sysctl -w net.ipv6.conf.all.forwarding=1")

    # links
    net.addLink(talker, n1, intfName1='eth0', intfName2='eth0')
    net.addLink(n1, n2, intfName1='eth12', intfName2='eth21')
    net.addLink(n1, n3, intfName1='eth13', intfName2='eth31')
    net.addLink(n2, n3, intfName1='eth23', intfName2='eth32')
    # Uncomment line below for multi-stage network
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
        n.cmd(f"ip a a 10.0.0.{ip_lo}/32 dev lo")   # used to telnet
        # add vrf interfaces
        n.cmd("ip link add name vrf1 type vrf table 254")
        n.cmd("ip link set vrf1 mtu 2000 up")
        n.cmd(f"ip -6 addr add fd00:a2d2:0:0:0:1::{ip_lo}/96 dev vrf1")  # used for sending, this IP will be used as source for inner IPv6 source
        n.cmd(f"ip6tables -t mangle -A OUTPUT -s fd00:a2d2:0:0:0:1::{ip_lo}/96 -j HL --hl-inc 1")
        n.cmd("ip link add name ve1 type veth peer name ve2")
        n.cmd("ip link set ve1 mtu 2000 up")
        n.cmd("ip link set ve2 mtu 2000 up")
        n.cmd(f"ip -6 addr add fd00:a2d2:0:0:0:2::{ip_lo}/96 dev ve1")
        n.cmd(f"ip -6 addr add fd00:a2d2:0:0:0:3::{ip_lo}/96 dev ve2")   # R2DTWO requires that IP type interfaces must have IP address
        # add dummy loopback interfaces for local SIDs (loopback interfaces do not work with SRv6)
        n.cmd("ip link add name sr0 type dummy")
        n.cmd(f"ip a a fd1{ip_lo}:fade::0/64 dev sr0")
        n.cmd("ip link set sr0 up")
        # add veth interfaces (not needed for TSN over SRv6)
        n.cmd("ip link add veth0 type veth peer name veth1")
        n.cmd("ip link set veth0 mtu 2000 up")
        n.cmd("ip link set veth1 mtu 2000 up")
        # add ARP entry for ve1 and local DT6 end
        n.cmd("ip -6 neigh add fd00:a2d2:0:0:0:2::99 lladdr 04:01:02:03:04:05 dev ve1")
        n.cmd(f"ip -6 route add fd1{ip_lo}:fade:0:0:1::/128 encap seg6local action End.DT6 count table 254 dev ve1")

        ip_lo += 1

    # t.cmd("ip a a 192.168.1.1/24 dev eth0")
    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")
    # l.cmd("ip a a 192.168.1.2/24 dev eth0")

    info('*** Adding router IPv6 addresses\n')
    n1.cmd("ip a a fd01:a1fa::1/64 dev eth0")
    n1.cmd("ip a a fd02:a1fa::1/64 dev eth12")
    n1.cmd("ip a a fd03:a1fa::1/64 dev eth13")
    n2.cmd("ip a a fd02:a1fa::2/64 dev eth21")
    n2.cmd("ip a a fd04:a1fa::2/64 dev eth23")
    n2.cmd("ip a a fd05:a1fa::2/64 dev eth24")
    n3.cmd("ip a a fd03:a1fa::3/64 dev eth31")
    n3.cmd("ip a a fd04:a1fa::3/64 dev eth32")
    n3.cmd("ip a a fd06:a1fa::3/64 dev eth34")
    n4.cmd("ip a a fd05:a1fa::4/64 dev eth42")
    n4.cmd("ip a a fd06:a1fa::4/64 dev eth43")
    n4.cmd("ip a a fd07:a1fa::4/64 dev eth0")
    t.cmd("ip a a fd01:a1fa::a/64 dev eth0")
    l.cmd("ip a a fd07:a1fa::e/64 dev eth0")

    # routing entries for interface IPs
    t.cmd("ip -6 r add ::/0 via fd01:a1fa::1 dev eth0")  # def. gw is n1
    n1.cmd("ip -6 r add ::/0 via fd02:a1fa::2 dev eth12")  # def. path is via n2
    n2.cmd("ip -6 r add fd07:a1fa::/64 via fd02:a1fa::1 dev eth21")
    n2.cmd("ip -6 r add fd01:a1fa::/64 via fd05:a1fa::4 dev eth24")
    n3.cmd("ip -6 r add fd07:a1fa::/64 via fd03:a1fa::1 dev eth31")
    n3.cmd("ip -6 r add fd01:a1fa::/64 via fd06:a1fa::4 dev eth34")
    n4.cmd("ip -6 r add ::/0 via fd05:a1fa::2 dev eth42")  # def. path is via n2
    l.cmd("ip -6 r add ::/0 via fd07:a1fa::4 dev eth0")  # def. gw is n4

    # set up routes for SID locators
    n1.cmd("ip -6 route add fd12:fade::/64 via fd02:a1fa::2 dev eth12")
    n1.cmd("ip -6 route add fd13:fade::/64 via fd03:a1fa::3 dev eth13")
    n1.cmd("ip -6 route add fd14:fade::/64 via fd02:a1fa::2 dev eth12")
    n2.cmd("ip -6 route add fd11:fade::/64 via fd02:a1fa::1 dev eth21")
    n2.cmd("ip -6 route add fd13:fade::/64 via fd04:a1fa::3 dev eth23")
    n2.cmd("ip -6 route add fd14:fade::/64 via fd05:a1fa::4 dev eth24")
    n3.cmd("ip -6 route add fd11:fade::/64 via fd03:a1fa::1 dev eth31")
    n3.cmd("ip -6 route add fd12:fade::/64 via fd04:a1fa::2 dev eth32")
    n3.cmd("ip -6 route add fd14:fade::/64 via fd06:a1fa::4 dev eth34")
    n4.cmd("ip -6 route add fd11:fade::/64 via fd06:a1fa::3 dev eth43")
    n4.cmd("ip -6 route add fd12:fade::/64 via fd05:a1fa::2 dev eth42")
    n4.cmd("ip -6 route add fd13:fade::/64 via fd06:a1fa::3 dev eth43")

    # add TC filters for UNI traffic
    n1.cmd("tc qdisc add dev eth0 handle ffff: ingress")
    n1.cmd("tc filter add dev eth0 parent ffff: protocol ipv6 flower dst_ip fd07:a1fa::e action mirred egress redirect dev veth0")
    n4.cmd("tc qdisc add dev eth0 handle ffff: ingress")
    n4.cmd("tc filter add dev eth0 parent ffff: protocol ipv6 flower dst_ip fd01:a1fa::a action mirred egress redirect dev veth0")


    info('*** Setting up SRv6 tunnels\n')
    # set up direct tunnels to n2 and n3
    # SIDs: fd00:a2d2:0:{node}:{path}
    n1.cmd("ip -6 route add fd00:a2d2:0:2:1::/80 encap seg6 mode encap segs fd12:fade:0:0:1:: dev eth12")
    n1.cmd("ip -6 route add fd00:a2d2:0:3:1::/80 encap seg6 mode encap segs fd13:fade:0:0:1:: dev eth13")
    n1.cmd("ip -6 route add fd00:a2d2:0:1::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")
    #n1.cmd("ip -6 route add fd00:a2d2:0:3::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")

    n2.cmd("ip -6 route add fd00:a2d2:0:4:1::/80 encap seg6 mode encap segs fd14:fade:0:0:1:: dev eth24")
    n2.cmd("ip -6 route add fd00:a2d2:0:3:1::/80 encap seg6 mode encap segs fd13:fade:0:0:1:: dev eth23")
    n2.cmd("ip -6 route add fd00:a2d2:0:2::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")
    #n2.cmd("ip -6 route add fd00:a2d2:0:3::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")

    n3.cmd("ip -6 route add fd00:a2d2:0:4:1::/80 encap seg6 mode encap segs fd14:fade:0:0:1:: dev eth34")
    #n3.cmd("ip -6 route add fd00:a2d2:0:2:2::/80 encap seg6 mode encap segs fd13:fade:0:0:1:: dev eth32")
    n3.cmd("ip -6 route add fd00:a2d2:0:3::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")
    #n3.cmd("ip -6 route add fd00:a2d2:0:2::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")

    # set up reverse tunnels: direct form n4 to n1
    n4.cmd("ip -6 route add fd00:a2d2:0:1:1::/80 encap seg6 mode encap segs fd12:fade::0,fd11:fade:0:0:1:: dev eth42")
    n4.cmd("ip -6 route add fd00:a2d2:0:1:2::/80 encap seg6 mode encap segs fd13:fade::0,fd11:fade:0:0:1:: dev eth43")
    n4.cmd("ip -6 route add fd00:a2d2:0:4::/64 via fd00:a2d2:0:0:0:2::99 table 254 dev ve1")


    # delay
    n1.cmd("tc qdisc add dev eth13 root netem delay 40ms")
    n1.cmd("tc qdisc add dev eth12 root netem delay 2ms")
    n2.cmd("tc qdisc add dev eth24 root netem delay 40ms")
    n2.cmd("tc qdisc add dev eth23 root netem delay 4ms")
    n2.cmd("tc qdisc add dev eth21 root netem delay 4ms")
    n3.cmd("tc qdisc add dev eth31 root netem delay 6ms")
    n4.cmd("tc qdisc add dev eth43 root netem delay 8ms")
    # n3.cmd("tc qdisc add dev eth34 root netem delay 10ms")

    info('*** Starting network\n')
    net.start()

    return net


def start_r2dtwos(net, debug):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            #node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo oam_srv6/singlestage/{n}.cfg -v PACKETTRACE:PACKET")
            node.popen(f"../r2dtwo -of oam_srv6/singlestage/{n}.cfg -v PACKETTRACE:PACKET")    # in general this is enough for debug
#            node.popen(f"../r2dtwo -of oam_srv6/singlestage/{n}.cfg -v ALL:ALL")    # in general this is enough for debug
        else:
            # node.popen(f"xterm -T {n} -e env -i gdb -nx -ex=r --args ../r2dtwo oam_srv6/singlestage/{n}.cfg -vALL:NONE")
            node.popen(f"../r2dtwo oam_srv6/singlestage/{n}.cfg -vALL:NONE")
#            node.popen(f"../r2dtwo -of oam_srv6/singlestage/{n}.cfg -v PACKETTRACE:PACKET")    # in general this is enough for debug

# list of (sender node, telnet command, expected reply)
testcases = [
    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3',
"""
OAM request ping session 2 seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:2 seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n2-i3-12 3 -n 3 -i 0.001',
"""
OAM request ping session 3 seq 0, s1n1-e4-01 -> s1n2-i3-12 level 3 count 3 interval 2, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:3 seq 0 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
  oam_r s1:3 seq 1 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
  oam_r s1:3 seq 2 lvl 3 R - ping on stream s1 target s1n2-i3-12; reply from s1n2-i3-12
"""
     ),


    ('n1', 'ping s1n1-e4-01 s1n3-i4-23 4',
"""
OAM request ping session 4 seq 0, s1n1-e4-01 -> s1n3-i4-23 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target s1n3-i4-23; reply from s1n3-i4-23
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-i4-34 4',
"""
OAM request ping session 5 seq 0, s1n1-e4-01 -> s1n4-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:5 seq 0 lvl 4 R - ping on stream s1 target s1n4-i4-34; reply from s1n4-i4-34
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -o',
"""
OAM request ping session 6 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: yes	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:6 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	Object pef4 type seqrec
		recovery_algorithm vector, reset_timer 2000ms
		use_init_flag false, use_reset_flag false, history_length 2
		history_content ...
		latent_error_paths 2, latent_error_resets 0, latent_errors 0
		latest_valid_sequence_number 0, passed 0, discarded 0
		number_of_resets 1
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 1',
"""
OAM request ping session 7 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:7 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n3-i4-13
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 2',
"""
OAM request ping session 8 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:8 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n3-i4-23
  oam_r s1:8 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-i4-34
  oam_r s1:8 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-i4-24
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 3',
"""
OAM request ping session 9 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:9 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-i4-34
  oam_r s1:9 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -t 4',
"""
OAM request ping session 10 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:10 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -d',
"""
OAM request ping session 11 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:11 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40 delay 0
"""
     ),

    ('n1', 'ping s1n1-e4-01 any 4',
"""
OAM request ping session 12 seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
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
OAM request ping session 13 seq 0, s1n1-e4-01 -> s1n4-i4-24 level 4 count 1 interval 1000, rr: yes os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:13 seq 0 lvl 4 R - ping on stream s1 target s1n4-i4-24; reply from s1n4-i4-24
	Record Route: [ s1n1-e4-01 s1n4-i4-24 ]
"""
     ),

    ('n1', 'ping s1n1-e4-01 s1n4-e4-40 4 -r',
"""
OAM request ping session 14 seq 0, s1n1-e4-01 -> s1n4-e4-40 level 4 count 1 interval 1000, rr: yes os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:14 seq 0 lvl 4 R - ping on stream s1 target s1n4-e4-40; reply from s1n4-e4-40
	Record Route: [ s1n1-e4-01 s1n3-i4-23 s1n3-i4-34 s1n4-i4-34 s1n4-e4-40 ]
"""
    ),

    (
        'n1', 'rlist s1n1-e4-01 any 4',
"""
OAM request rlist session 15 seq 0, s1n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
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
OAM request rlist session 0 seq 0, s1n1-e4-01 -> any level 3 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
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
OAM request rping session 2 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:2 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:2 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:2 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 any 4',
"""
OAM request rping session 3 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:3 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:3 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-13 any 4',
"""
OAM request rping session 4 seq 0, s1n1-e4-01 -> s1n3-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n3-i4-34
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-i4-34
  oam_r s1:4 seq 0 lvl 4 R - ping on stream s1 target any; reply from s1n4-e4-40
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-34 4 s1n3-i4-34 any 4',
"""
OAM request rping session 5 seq 0, s1n1-e4-01 -> s1n3-i4-34 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
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
OAM request ping session 1 seq 0, s2n4-e5-04 -> s2n1-i5-21 level 5 count 1 interval 1000, rr: no os: no	[reply to ip fd14:fade::0 port 6634]
  oam_r s2:1 seq 0 lvl 5 R - ping on stream s2 target s2n1-i5-21; reply from s2n1-i5-21
"""
    ),
    (
        'n4', 'ping s2n4-e5-04 s2n1-i5-31 5',
"""
OAM request ping session 2 seq 0, s2n4-e5-04 -> s2n1-i5-31 level 5 count 1 interval 1000, rr: no os: no	[reply to ip fd14:fade::0 port 6634]
  oam_r s2:2 seq 0 lvl 5 R - ping on stream s2 target s2n1-i5-31; reply from s2n1-i5-31
"""
    ),
    (
        'n4', 'ping s2n4-e5-04 s2n1-e5-10 5',
"""
OAM request ping session 3 seq 0, s2n4-e5-04 -> s2n1-e5-10 level 5 count 1 interval 1000, rr: no os: no	[reply to ip fd14:fade::0 port 6634]
  oam_r s2:3 seq 0 lvl 5 R - ping on stream s2 target s2n1-e5-10; reply from s2n1-e5-10

"""
    ),

    (
        'n1', 'ping s3n1-e4-01 any 4',
"""
OAM request ping session 2 seq 0, s3n1-e4-01 -> any level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n3-i4-13
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-i4-34
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-i4-24
  oam_r s3:2 seq 0 lvl 4 R - ping on stream s3 target any; reply from s3n4-e4-40
"""
    ),
    (
        'n3', 'ping s3n3-e1-32 any 1',
"""
OAM request ping session 1 seq 0, s3n3-e1-32 -> any level 1 count 1 interval 1000, rr: no os: no	[reply to ip fd13:fade::0 port 6634]
  oam_r tx332:1 seq 0 lvl 1 R - ping on stream tx332 target any; reply from s3n4-e1-24
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 nonexistentmp 4 s1n3-i4-34 any 4',
"""
OAM request rping session 6 seq 0, s1n1-e4-01 -> nonexistentmp level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 s1n3-i4-34 nonexistentmp 4',
"""
OAM request rping session 7 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
"""
    ),

    (
        'n1', 'rping s1n1-e4-01 s1n3-i4-13 4 nonexistentmp any 4',
"""
OAM request rping session 8 seq 0, s1n1-e4-01 -> s1n3-i4-13 level 4 count 1 interval 1000, rr: no os: no	[reply to ip fd11:fade::0 port 6634]
Rping error from s1n3-i4-13 : could not create ping request: ping start 'nonexistentmp' invalid
"""
    ),

    (
        'n1', 'sessions', # note: exiting telnet clears the associated sessions
"""
Stream s1 sessions:
    1 ping s1n1-e4-01 -> s1n4-e3-24 level 3 connection <background>
Stream s3 sessions:
    1 ping s3n1-e4-01 -> s3n4-e4-40 level 4 connection <background>
"""
    ),

    (
        'n1', 'sessions s3', # note: exiting telnet clears the associated sessions
"""
Stream s3 sessions:
    1 ping s3n1-e4-01 -> s3n4-e4-40 level 4 connection <background>
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
    time.sleep(2.5)
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
            reply = re.sub(r'delay -?\d+\.\d+',
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
            print("R2DTWO OAM SRv6 debug")
        else:
            print("R2DTWO OAM SRv6 test")
        net = create_net()
        #setLogLevel('debug')
        config_net(net)
        if debug:
            inp = input("Do you want to start R2DWOs? (yes/no): ")
            if inp.lower() in ["yes", "y"]:
#                start_r2dtwos(net, False)
                start_r2dtwos(net, debug)
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
