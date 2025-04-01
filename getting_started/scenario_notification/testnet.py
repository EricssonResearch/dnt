#!/usr/bin/python3
from mininet.net import Mininet
from mininet.cli import CLI
import time

"""
      ╔═══════╗                             ╔═══════╗    
      ║plague ║                             ║ dread ║    
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
│                │       │           │       │         │ 
│                │       │           │       │         │ 
│          ens2f1├───────│ens2f1     │       └╔═══════╗┘ 
└───╔═══════╗────┘       └─╔═══════╗─┘        ║minion ║  
    ║pandora║              ║terror ║          ╚═══════╝  
    ╚═══════╝              ╚═══════╝           R2DTWO    
     R2DTWO
"""

def main():
    import os
    os.system("killall xterm")
    net = Mininet()
    hosts = ["plague", "pandora", "terror", "minion", "dread"]
    for hostname in hosts:
        net.addHost(hostname, ip=None)
    plague, pandora, terror, minion, dread = [net.get(n) for n in hosts]

    net.addLink(plague, pandora, intfName1='ue0', intfName2='eno1')
    net.addLink(pandora, terror, intfName1='ens2f0', intfName2='ens2f0')
    net.addLink(pandora, terror, intfName1='ens2f1', intfName2='ens2f1')
    net.addLink(terror, minion, intfName1='eno0', intfName2='eno1')
    net.addLink(minion, dread, intfName1='eno2', intfName2='ue0')

    net.build()

    # disable offload
    plague.cmd("ethtool -K ue0 tx off rx off")
    dread.cmd("ethtool -K ue0 tx off rx off")

    # addressing
    plague.cmd("ip a a 192.168.2.2/24 dev ue0")
    pandora.cmd("ip a a 192.168.2.1/24 dev eno1")
    pandora.cmd("ip a a 192.168.1.2/24 dev ens2f0")
    pandora.cmd("ip a a 192.168.0.2/24 dev ens2f1")
    terror.cmd("ip a a 192.168.1.1/24 dev ens2f0")
    terror.cmd("ip a a 192.168.0.1/24 dev ens2f1")
    terror.cmd("ip a a 10.10.10.1/24 dev eno0")
    minion.cmd("ip a a 10.10.10.2/24 dev eno1")
    minion.cmd("ip a a 10.10.11.1/24 dev eno2")
    dread.cmd("ip a a 10.10.11.2/24 dev ue0")

    # routing
    plague.cmd("ip r add default via 192.168.2.1")
    pandora.cmd("ip r add 10.10.10.0/24 via 192.168.1.1 metric 1")
    pandora.cmd("ip r add 10.10.10.0/24 via 192.168.0.1 metric 10")
    pandora.cmd("ip r add 10.10.11.0/24 blackhole")
    terror.cmd("sysctl -w net.ipv4.ip_forward=1")
    minion.cmd("ip r add 192.168.1.0/24 via 10.10.10.1")
    minion.cmd("ip r add 192.168.0.0/24 via 10.10.10.1")

    # this rule drop false positive ICMP errors
    # talker-to-listener packets are tunneled by R2DTWO and they reach their destination
    # but pandora's routing table dont have dread's prefix and generate ICMP net unreach error
    # to prevent this, we drop plague's packet right after R2DTWO's tap
    pandora.cmd("iptables -A PREROUTING -t raw -d 10.10.11.0/24 -j DROP")
    # pandora.cmd("tc qdisc add dev eno1 ingress")
    # pandora.cmd("tc filter add dev eno1 parent ffff: u32 match ip dst 10.10.11.0/24 action drop")
    dread.cmd("ip r add default via 10.10.11.1")

    # start r2dtwos in background
    minion.popen(f"xterm -T minion -e python3 ../../json_receiver/json_udp_receiver.py 10.10.10.2 6000")
    pandora.popen(f"xterm -T pandora -e r2dtwo pandora.ini -v ALL:NONE,PACKETTRACE:DEBUG")
    minion.popen(f"xterm -T minion -e r2dtwo minion.ini -v ALL:NONE,PACKETTRACE:DEBUG")
    time.sleep(1)
    pandora.popen(f"xterm -T pandora -e telnet localhost 8000")
    minion.popen(f"xterm -T minion -e telnet localhost 8000")

    # in CLI, execute: plague ping dread
    CLI(net)
    net.stop()


main()
