#!/usr/bin/python3

from scapy.all import AsyncSniffer, sendp, subprocess
from scapy.layers.l2 import Ether, Dot1Q
from scapy.layers.inet import IP, UDP
from subprocess import Popen, call, PIPE
import shlex
import time
import sys
import os

r2stdout = None

def exec_with_stdout(cmd):
    global stdouts
    p = Popen(shlex.split(cmd), pipesize=100000000, stdout=PIPE) #python 3.10 required
    r2stdout = p
    return r2stdout

def start_r2dtwo():
    exec_with_stdout("../../r2dtwo match.ini")

def config_ifaces():
    ret = 0
    ret += call(shlex.split("ip link del to_r2 type veth peer name r2rx"))
    ret += call(shlex.split("ip link del from_r2 type veth peer name r2tx"))

    ret += call(shlex.split("ip link add to_r2 type veth peer name r2rx"))
    ret += call(shlex.split("ip link add from_r2 type veth peer name r2tx"))
    ret += call(shlex.split("sysctl -w net.ipv6.conf.r2rx.disable_ipv6=1"))
    ret += call(shlex.split("sysctl -w net.ipv6.conf.to_r2.disable_ipv6=1"))
    ret += call(shlex.split("sysctl -w net.ipv6.conf.r2tx.disable_ipv6=1"))
    ret += call(shlex.split("sysctl -w net.ipv6.conf.from_r2.disable_ipv6=1"))
    ret += call(shlex.split("ip link set dev to_r2 up"))
    ret += call(shlex.split("ip link set dev r2rx up"))
    ret += call(shlex.split("ip link set dev from_r2 up"))
    ret += call(shlex.split("ip link set dev r2tx up"))
    if ret > 1:
        print("Error(s) during interface config. Running without sudo?")
        exit(1)

def test_eth():
    config_ifaces()
    start_r2dtwo()
    time.sleep(1)
    receiver = AsyncSniffer(iface='from_r2')
    receiver.start()

    pkts_good = [
        Ether()/Dot1Q(vlan=2023)/IP()/UDP(),
        Ether()/Dot1Q(vlan=1111)/IP(src="1.2.3.4")/UDP(),
    ]

    pkts_bad = []

    for pkt in pkts_good:
        sendp(pkt, iface="to_r2")
    receiver.stop()
    if len(receiver.results) != len(pkts_good):
        return 0
    return 1

def main():
    print("R2DTWO match test")
    ret = 0
    tests = [test_eth]
    for test in tests:
        ret += test()
    print(f'All test completed, {ret}/{len(tests)} successfully')
    if ret != len(tests):
        exit(1)
    exit(0)

main()
