#!/bin/env python3

# this is based on the bird demonstation for hta2022

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

    # config addresses
    na.cmd("ip a a 10.0.1.1/24 dev ethAB")
    nb.cmd("ip a a 10.0.1.2/24 dev ethBA")

    nb.cmd("ip a a 10.0.2.2/24 dev ethBC")
    nc.cmd("ip a a 10.0.2.3/24 dev ethCB")

    nc.cmd("ip a a 10.0.3.3/24 dev ethCD")
    nd.cmd("ip a a 10.0.3.4/24 dev ethDC")

    nd.cmd("ip a a 10.0.4.4/24 dev ethDA")
    na.cmd("ip a a 10.0.4.1/24 dev ethAD")


    na.cmd("ip l a link ethAB name ethAB.r2tunnel type vlan id 100")
    na.cmd("ip a a 192.168.1.1/24 dev ethAB.r2tunnel")
    na.cmd("ip l set up dev ethAB.r2tunnel")
    nc.cmd("ip l a link ethCB name ethCB.r2tunnel type vlan id 100")
    nc.cmd("ip a a 192.168.1.3/24 dev ethCB.r2tunnel")
    nc.cmd("ip l set up dev ethCB.r2tunnel")

    #TODO start r2dthree on nb
    #TODO on na: ping 192.168.1.3

    return net

if __name__ == '__main__':
    net = start_network()
    CLI(net)
    net.stop()
    #os.system("killall bird")


