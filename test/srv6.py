#!/usr/bin/python

from mininet.net import Mininet
from mininet.node import Host, Node
from mininet.cli import CLI
from mininet.log import setLogLevel, info
from functools import partial
import time
import sys

def setup_network():
    """
    For basic SRv6 testing.
               r2
              /   \
    t1 ---- r1-----r3 ---- l1

    test command: t1 ping6 fd05::6

    """
    net = Mininet(waitConnected=True)

    info('*** Adding hosts\n')
    hostnames = ['t1', 'r1', 'r2', 'r3', 'l1']
    t1, r1, r2, r3, l1 = [net.addHost(n) for n in hostnames]

    for node in [t1, r1, r2, r3, l1]:
        node.cmd("sysctl -w net.ipv6.conf.all.seg6_enabled=1")
        node.cmd("sysctl -w net.ipv6.conf.default.seg6_enabled=1")
        node.cmd("sysctl -w net.ipv6.conf.all.forwarding=1")

    info('*** Creating links\n')
    net.addLink(t1, r1, intfName1='eth_t1r1', intfName2='eth_r1t1')
    net.addLink(r1, r2, intfName1='eth_r1r2', intfName2='eth_r2r1')
    net.addLink(r1, r3, intfName1='eth_r1r3', intfName2='eth_r3r1')
    net.addLink(r2, r3, intfName1='eth_r2r3', intfName2='eth_r3r2')
    net.addLink(r3, l1, intfName1='eth_r3l1', intfName2='eth_l1r3')

    net.build()

    r1.cmd("ip link add name loop1 type vrf table 10")
    r1.cmd("ip link set loop1 up")
    r1.cmd("ip -6 rule del priority 1000")
    r1.cmd("ip -6 rule add type blackhole l3mdev to fc:ff00:0:2::/64 priority 1000")
    r1.cmd("ip -6 rule add table local priority 2000")

    r3.cmd("ip link add name loop1 type vrf table 10")
    r3.cmd("ip link set loop1 up")
    r3.cmd("ip -6 rule del priority 1000")
    r3.cmd("ip -6 rule add type blackhole l3mdev to fc:ff00:0:1::/64 priority 1000")
    r3.cmd("ip -6 rule add table local priority 2000")

    # block ICMPv6 host unreachable   -- TODO: solve this
    r1.cmd("ip6tables -I OUTPUT -p icmpv6 --icmpv6-type 1 -j DROP")
    r3.cmd("ip6tables -I OUTPUT -p icmpv6 --icmpv6-type 1 -j DROP")

    info('*** Adding IPv6 addresses\n')
    t1.cmd("ip a a fd01::1/64 dev eth_t1r1")
    r1.cmd("ip a a fd01::2/64 dev eth_r1t1")
    r1.cmd("ip a a fd02::2/64 dev eth_r1r2")
    r2.cmd("ip a a fd02::3/64 dev eth_r2r1")
    r1.cmd("ip a a fd03::3/64 dev eth_r1r3")
    r3.cmd("ip a a fd03::4/64 dev eth_r3r1")
    r2.cmd("ip a a fd04::4/64 dev eth_r2r3")
    r3.cmd("ip a a fd04::5/64 dev eth_r3r2")
    r3.cmd("ip a a fd05::5/64 dev eth_r3l1")
    l1.cmd("ip a a fd05::6/64 dev eth_l1r3")

    # routing
    t1.cmd("ip -6 r add ::/0 via fd01::2 dev eth_t1r1")  # def. gw is r1
    #r1.cmd("ip -6 r add ::/0 via fd03::4 dev eth_r1r3")  # def. path is via r3
    r2.cmd("ip -6 r add fd05::6/64 via fd04::5 dev eth_r2r3")
    r2.cmd("ip -6 r add fd01::1/64 via fd02::2 dev eth_r2r1")
    #r3.cmd("ip -6 r add ::/0 via fd03::3 dev eth_r3r1")  # def. path is via r1
    l1.cmd("ip -6 r add ::/0 via fd05::5 dev eth_l1r3")  # def. gw is r3


    # set up SIDs in R2. Needed if no decap SID in r1/r3
    r2.cmd("ip -6 route add fc:ff00:0:1::/64 via fd04::5 dev eth_r2r3")
    r2.cmd("ip -6 route add fc:ff00:0:2::/64 via fd02::2 dev eth_r2r1")


    # set up direct tunnel
    r1.cmd("ip -6 route add fc:ff00:0:1::/64 encap seg6 mode encap segs fd02::3,fd04::5 dev eth_r1r2")
    r3.cmd("ip -6 addr add fd00::3/64 dev loop1")
    r3.cmd("ip -6 route add fc:ff00:0:1::/64  dev loop1")
    #r3.cmd("ip -6 route add fc:ff00:0:1/64 encap seg6local action End.DT6 table 254 dev loop1")

    # set up reverse tunnel
    r3.cmd("ip -6 route add fc:ff00:0:2::/64 encap seg6 mode encap segs fd04::4,fd02::2 dev eth_r3r2")
    r1.cmd("ip -6 addr add fd00::1/64 dev loop1")
    r1.cmd("ip -6 route add fc:ff00:0:2::/64  dev loop1")
    #r1.cmd("ip -6 route add fc:ff00:0:2::/64 encap seg6local action End.DT6 table 254 dev loop1")

    info('*** Starting network\n')
    net.start()

    #r3.cmd("xterm &")

    return net

def start_r2dtwos(net, debug):
    # start R2DTWOs
    for n in ['r1', 'r3']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo srv6/{n}.cfg -v ALL:ALL")
        else:
            node.popen(f"../r2dtwo -of srv6/{n}.cfg")

def stop_network(net):
    info('*** Stopping network')
    net.stop()

if __name__ == '__main__':
    debug = False
    if len(sys.argv) >= 2 and sys.argv[1] == "debug":
        debug = True
    if debug:
        print("R2DTWO SRv6 debug")
    else:
        print("R2DTWO SRv6 test")
    setLogLevel('info')
    net = setup_network()
    info('*** Starting R2DRWOs\n')
    start_r2dtwos(net, debug)
    CLI(net)
    stop_network(net)
