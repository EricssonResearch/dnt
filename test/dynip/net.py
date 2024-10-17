#!/bin/env python3

from mininet.net import Mininet
from mininet.cli import CLI

import os

def start_network():
    net = Mininet()

    # create  nodes
    na = net.addHost('na', ip=None)
    nb = net.addHost('nb', ip=None)
    nc = net.addHost('nc', ip=None)
    nd = net.addHost('nd', ip=None)
    ne = net.addHost('ne', ip=None)

    # link them
    net.addLink(na, nb, intfName1='ethAB', intfName2='ethBA')
    net.addLink(nb, nc, intfName1='ethBC', intfName2='ethCB')
    net.addLink(nc, nd, intfName1='ethCD', intfName2='ethDC')
    net.addLink(nd, ne, intfName1='ethDE', intfName2='ethED')

    net.build()

    # config addresses, disable tx checksum offload
    na.cmd("ip a a 10.0.1.1/24 dev ethAB; ethtool -K ethAB tx off")
    nb.cmd("ip a a 10.0.1.2/24 dev ethBA; ethtool -K ethBA tx off")
    na.cmd("ip a a fd01::1/64 dev ethAB")
    nb.cmd("ip a a fd01::2/64 dev ethBA")

    nb.cmd("ip a a 10.0.2.2/24 dev ethBC; ethtool -K ethBC tx off")
    nc.cmd("ip a a 10.0.2.3/24 dev ethCB; ethtool -K ethCB tx off")
    nb.cmd("ip a a fd02::2/64 dev ethBC")
    nc.cmd("ip a a fd02::3/64 dev ethCB")

    # on this link nc will run DHCP/RA to supply address for nd
    nc.cmd("ip a a 10.0.3.3/24 dev ethCD; ethtool -K ethCD tx off")
    nc.cmd("ip a a fd03::3/64 dev ethCD")
    nd.cmd("ethtool -K ethDC tx off")

    nd.cmd("ip a a 10.0.4.4/24 dev ethDE; ethtool -K ethDE tx off")
    ne.cmd("ip a a 10.0.4.5/24 dev ethED; ethtool -K ethED tx off")
    nd.cmd("ip a a fd04::4/64 dev ethDE")
    ne.cmd("ip a a fd04::5/64 dev ethED")

    nb.cmd("ip l set dev ethBC mtu 2000 up")
    nc.cmd("ip l set dev ethCB mtu 2000 up")
    nc.cmd("ip l set dev ethCD mtu 2000 up")
    nd.cmd("ip l set dev ethDC mtu 2000 up")

    na.cmd("ip r a default via 10.0.1.2 dev ethAB") # reach e via b
    nb.cmd("ip r a default via 10.0.2.3 dev ethBC") # reach d via c
    nd.cmd("ip r a default via 10.0.3.3 dev ethDC") # reach b via c
    ne.cmd("ip r a default via 10.0.4.4 dev ethED") # reach a via d
    na.cmd("ip r a default via fd01::2 dev ethAB") # reach e via b
    nb.cmd("ip r a default via fd02::3 dev ethBC") # reach d via c
    nd.cmd("ip r a default via fd03::3 dev ethDC") # reach b via c
    ne.cmd("ip r a default via fd04::4 dev ethED") # reach a via d

    nc.cmd("sysctl net.ipv4.ip_forward=1")
    nc.cmd("sysctl net.ipv6.conf.all.forwarding=1")

    # don't generate net unreachable (must have for iperf!)
    nb.cmd("ip r a blackhole 10.0.4.0/24")
    nd.cmd("ip r a blackhole 10.0.1.0/24")
    nb.cmd("ip r a blackhole fd04::/64")
    nd.cmd("ip r a blackhole fd01::/64")

    return net

if __name__ == '__main__':
    net = start_network()
    CLI(net)
    net.stop()

