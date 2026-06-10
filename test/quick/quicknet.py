#!/bin/env python3

from mininet.net import Mininet
from mininet.cli import CLI

import os

def start_network():
    net = Mininet()

    # create four nodes
    na = net.addHost('na', ip=None)
    nb = net.addHost('nb', ip=None)
    nc = net.addHost('nc', ip=None)
    nd = net.addHost('nd', ip=None)

    # link them in a square
    net.addLink(na, nb, intfName1='ethAB', intfName2='ethBA')
    net.addLink(nb, nc, intfName1='ethBC', intfName2='ethCB')
    net.addLink(nc, nd, intfName1='ethCD', intfName2='ethDC')
    net.addLink(nd, na, intfName1='ethDA', intfName2='ethAD')

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

    nc.cmd("ip a a 10.0.3.3/24 dev ethCD; ethtool -K ethCD tx off")
    nd.cmd("ip a a 10.0.3.4/24 dev ethDC; ethtool -K ethDC tx off")
    nc.cmd("ip a a fd03::3/64 dev ethCD")
    nd.cmd("ip a a fd03::4/64 dev ethDC")

    nd.cmd("ip a a 10.0.4.4/24 dev ethDA; ethtool -K ethDA tx off")
    na.cmd("ip a a 10.0.4.1/24 dev ethAD; ethtool -K ethAD tx off")
    nd.cmd("ip a a fd04::4/64 dev ethDA")
    na.cmd("ip a a fd04::1/64 dev ethAD")

    nb.cmd("ip l set dev ethBC mtu 2000 up")
    nc.cmd("ip l set dev ethCB mtu 2000 up")

    na.cmd("ip l a link ethAB name ethAB.r2tunnel type vlan id 1001")
    na.cmd("ip a a 192.168.1.1/24 dev ethAB.r2tunnel")
    na.cmd("ip a a fd92::1/64 dev ethAB.r2tunnel")
    na.cmd("ip l set up dev ethAB.r2tunnel")
    nc.cmd("ip l a link ethCB name ethCB.r2tunnel type vlan id 2023")
    nc.cmd("ip a a 192.168.1.3/24 dev ethCB.r2tunnel")
    nc.cmd("ip a a fd92::3/64 dev ethCB.r2tunnel")
    nc.cmd("ip l set up dev ethCB.r2tunnel")
    nd.cmd("ip l a link ethDC name ethDC.r2tunnel type vlan id 3210")
    nd.cmd("ip a a 192.168.1.4/24 dev ethDC.r2tunnel")
    nd.cmd("ip a a fd92::4/64 dev ethDC.r2tunnel")
    nd.cmd("ip l set up dev ethDC.r2tunnel")

    #TODO on nb: ../dnt quicktest.ini
    #TODO on na: ping 192.168.1.3
    #TODO on na: ping fd92::3

    #TODO on nb: ../dnt quickdetB.ini
    #TODO on nc: ../dnt quickdetC.ini
    #TODO on na: ping 192.126.1.4
    #TODO on na: ping fd92::4

    return net

if __name__ == '__main__':
    net = start_network()
    CLI(net)
    net.stop()
    #os.system("killall bird")


