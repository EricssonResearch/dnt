#!/usr/bin/python3

from socket import AF_INET, SOCK_STREAM, socket
from mininet.net import Mininet
from mininet.cli import CLI
from pyroute2 import netns
from utils import *
# import signal
# import time
# import sys


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
    n4.cmd("ip r a 12.0.0.0/24 via 24.0.0.2 metric 5")
    n4.cmd("ip r a 12.0.0.0/24 via 34.0.0.3 metric 10")
    n4.cmd("ip r a 13.0.0.0/24 via 34.0.0.3 metric 5")
    n4.cmd("ip r a 13.0.0.0/24 via 24.0.0.2 metric 10")

    # for OAM router-local address availability
    n1.cmd("ip r a default via 12.0.0.2")
    n2.cmd("ip r a default via 24.0.0.4")
    n3.cmd("ip r a default via 13.0.0.1")
    n4.cmd("ip r a default via 34.0.0.3")

def run_tests(net):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        node.popen(f"../r2dtwo oam/singlestage/{n}.cfg")
    # list of (sender, message, expected reply)
    testcases = [
        ('n1', 'ping stream_uni:mepn1s1 in12 4', None)
    ]
    for node, msg, reply in testcases:
        switch_netns(node)
        with socket(AF_INET, SOCK_STREAM, 0) as s:
            s.connect(('10.0.0.1', 8000))
            _ = s.recv(1024)
            s.sendall(msg.encode())
            rcvd_reply = s.recv(2000)
            print(rcvd_reply)
            rcvd_reply = s.recv(2000)
            print(rcvd_reply)
    switch_netns()

def main():
    print("R2DTWO OAM test")
    net = create_net()
    config_net(net)
    run_tests(net)
    CLI(net)
    net.stop()

if __name__ == "__main__":
    main()
