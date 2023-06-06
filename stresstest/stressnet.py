#!/bin/env python3

from mininet.net import Mininet
from mininet.cli import CLI

import os

def start_network():
    net = Mininet()

    # network is based on getting_started/scenario1

    talker = net.addHost('talker', ip=None)
    listener = net.addHost('listener', ip=None)
    nxp1 = net.addHost('nxp1', ip=None)
    nxp2 = net.addHost('nxp2', ip=None)

    net.addLink(talker, nxp1, intfName1='eth0', intfName2='swp2')
    net.addLink(listener, nxp2, intfName1='eth0', intfName2='swp2')
    net.addLink(nxp1, nxp2, intfName1='swp0', intfName2='swp0')
    net.addLink(nxp1, nxp2, intfName1='swp1', intfName2='swp1')

    talker.cmd("ip a a 10.0.0.1/24 dev eth0; ip a a fd01::1/64 dev eth0")
    listener.cmd("ip a a 10.0.0.2/24 dev eth0; ip a a fd01::2/64 dev eth0")

    talker.cmd("ethtool -K eth0 tx off")
    listener.cmd("ethtool -K eth0 tx off")
    nxp1.cmd("ethtool -K swp0 tx off; ip link set swp0 mtu 1536 up")
    nxp1.cmd("ethtool -K swp1 tx off; ip link set swp1 mtu 1536 up")
    nxp1.cmd("ethtool -K swp2 tx off; ip link set swp2 mtu 1536 up")
    nxp2.cmd("ethtool -K swp0 tx off; ip link set swp0 mtu 1536 up")
    nxp2.cmd("ethtool -K swp1 tx off; ip link set swp1 mtu 1536 up")
    nxp2.cmd("ethtool -K swp2 tx off; ip link set swp2 mtu 1536 up")

    nxp1.cmd("tc qdisc add dev swp0 parent root netem limit 5000 delay 50ms")
    nxp2.cmd("tc qdisc add dev swp0 parent root netem limit 5000 delay 50ms")
    nxp1.cmd("tc qdisc add dev swp1 parent root netem loss 50%")
    nxp2.cmd("tc qdisc add dev swp1 parent root netem loss 50%")

    # on nxp1: ../r2dtwo stress.ini
    # on nxp2: ../r2dtwo stress.ini
    # on listener: iperf -Vs
    # on talker: iperf -c 10.0.0.2
    # on talker: iperf -c fd01::2

    return net

if __name__ == '__main__':
    net = start_network()
    CLI(net)
    net.stop()

