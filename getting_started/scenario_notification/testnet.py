#!/usr/bin/python3
from mininet.net import Mininet
from mininet.cli import CLI
import time

"""
      ╔═══════╗                             ╔═══════╗
      ║ host1 ║                             ║ host2 ║
  ┌───╚═══════╝───┐                      ┌──╚═══════╝───┐
  │   (talker)    │                      │  (listener)  │
  │               │                      │              │
  │ue0:192.168.2.2│                      │ue0:10.10.11.2│
  └────┬──────────┘                      └─────────┬────┘
       │                                           │
       │                                           │
┌──────┴─────────┐       ┌───────────┐             │
│    eno1        │       │           │             │
│                │       │           │       ┌─────┴───┐
│          ens2f0├───────┤ens2f0     │       │   eno2  │
│                │       │           │       │         │
│                │       │       eno1├───────┤eno1     │
│          ens2f1├───────│ens2f1     │       │         │
│                │       │           │       │  (pef)  │
│     (prf)      │       │           │       └╔═══════╗┘
└───╔═══════╗────┘       └─╔═══════╗─┘        ║ edge2 ║
    ║ edge1 ║              ║ core  ║          ╚═══════╝
    ╚═══════╝              ╚═══════╝           R2DTWO
     R2DTWO
"""

def main():
    import os
    import sys

    if len(sys.argv) == 1:
        automip = False
    elif len(sys.argv) == 2 and sys.argv[1].lower() == "automip":
        automip = True
    else:
        print("Unknown option. AutoMIP is the only accepted parameter.")
        exit(1)

    if automip:
        print("Starting AutoMIP version")
    else:
        print("Starting manual MIP version")


    os.system("killall xterm")
    net = Mininet()
    hosts = ["host1", "edge1", "core", "edge2", "host2"]
    for hostname in hosts:
        net.addHost(hostname, ip=None)
    host1, edge1, core, edge2, host2 = [net.get(n) for n in hosts]

    net.addLink(host1, edge1, intfName1='ue0', intfName2='eno1')
    net.addLink(edge1, core, intfName1='ens2f0', intfName2='ens2f0')
    net.addLink(edge1, core, intfName1='ens2f1', intfName2='ens2f1')
    net.addLink(core, edge2, intfName1='eno0', intfName2='eno1')
    net.addLink(edge2, host2, intfName1='eno2', intfName2='ue0')

    net.build()

    # disable offload
    host1.cmd("ethtool -K ue0 tx off rx off")
    host2.cmd("ethtool -K ue0 tx off rx off")

    # addressing
    host1.cmd("ip a a 192.168.2.2/24 dev ue0")
    edge1.cmd("ip a a 192.168.2.1/24 dev eno1")
    edge1.cmd("ip a a 192.168.1.2/24 dev ens2f0")
    edge1.cmd("ip a a 192.168.0.2/24 dev ens2f1")
    core.cmd("ip a a 192.168.1.1/24 dev ens2f0")
    core.cmd("ip a a 192.168.0.1/24 dev ens2f1")
    core.cmd("ip a a 10.10.10.1/24 dev eno0")
    edge2.cmd("ip a a 10.10.10.2/24 dev eno1")
    edge2.cmd("ip a a 10.10.11.1/24 dev eno2")
    host2.cmd("ip a a 10.10.11.2/24 dev ue0")

    # routing
    host1.cmd("ip r add default via 192.168.2.1")
    edge1.cmd("ip r add 10.10.10.0/24 via 192.168.1.1 metric 1")
    edge1.cmd("ip r add 10.10.10.0/24 via 192.168.0.1 metric 10")
    edge1.cmd("ip r add 10.10.11.0/24 blackhole")
    core.cmd("sysctl -w net.ipv4.ip_forward=1")
    edge2.cmd("ip r add 192.168.1.0/24 via 10.10.10.1")
    edge2.cmd("ip r add 192.168.0.0/24 via 10.10.10.1")
    edge2.cmd("ip r add 192.168.2.0/24 via 10.10.10.1")

    core.cmd("ip r add 192.168.2.0/24 via 192.168.0.2")
    core.cmd("ip r add 192.168.2.0/24 via 192.168.1.2")

    # this rule drop false positive ICMP errors
    # talker-to-listener packets are tunneled by R2DTWO and they reach their destination
    # but edge1's routing table dont have host2's prefix and generate ICMP net unreach error
    # to prevent this, we drop host1's packet right after R2DTWO's tap
    edge1.cmd("iptables -A PREROUTING -t raw -d 10.10.11.0/24 -j DROP")
    # edge1.cmd("tc qdisc add dev eno1 ingress")
    # edge1.cmd("tc filter add dev eno1 parent ffff: u32 match ip dst 10.10.11.0/24 action drop")
    host2.cmd("ip r add default via 10.10.11.1")

    edge2.popen(f"xterm -T edge2 -e python3 ../../json_receiver/multipart_json_udp_receiver.py 10.10.10.2 6000")
    # start r2dtwos in background
    if automip:
        edge1.popen(f"xterm -T edge1 -e r2dtwo edge1-automip.ini -h edge1 -v PACKETTRACE:ALL")
        edge2.popen(f"xterm -T edge2 -e r2dtwo edge2-automip.ini -h edge2 -v PACKETTRACE:ALL")
    else:
        edge1.popen(f"xterm -T edge1 -e r2dtwo edge1.ini -h edge1 -v PACKETTRACE:ALL")
        edge2.popen(f"xterm -T edge2 -e r2dtwo edge2.ini -h edge2 -v PACKETTRACE:ALL")

    time.sleep(1)
    edge1.popen(f"xterm -T edge1 -e telnet localhost 8000")
    edge2.popen(f"xterm -T edge2 -e telnet localhost 8000")

    # in CLI, execute: host1 ping host2
    CLI(net)
    net.stop()


main()
