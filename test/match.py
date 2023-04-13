#!/usr/bin/python3

from scapy.layers.l2 import Ether, Dot1Q
from scapy.layers.inet import IP, UDP
from scapy.layers.inet6 import IPv6
from scapy.all import sendp
from utils import *
import json
import time


pkts_good_eth = [
    Ether(dst="aa:bb:cc:aa:bb:cc")/Dot1Q(vlan=2025)/IP()/UDP(),
    Ether(dst="aa:bb:cc:ff:ff:ff", src="dd:ee:ff:dd:ee:ff")/Dot1Q(vlan=2025)/IP()/UDP(),
]
pkts_bad_eth = [
    Ether(dst="ab:bb:cc:aa:bb:cd")/Dot1Q(vlan=2025)/IP()/UDP(),
    Ether(dst="aa:bb:cc:ff:ff:ff", src="dd:ee:ff:dd:ee:fe")/Dot1Q(vlan=2025)/IP()/UDP(),
]

pkts_good_vlans = [
    Ether()/Dot1Q(vlan=2023)/IP()/UDP(),
    Ether()/Dot1Q(vlan=1111, prio=5)/IP(src="1.2.3.4")/UDP(),
    Ether()/Dot1Q(vlan=99, prio=2)/IP(src="1.2.3.4")/UDP(),
    Ether()/Dot1Q(vlan=66, prio=2)/IP(src="1.2.3.4")/UDP(),
]
pkts_bad_vlans = [
    Ether()/Dot1Q(vlan=1111, prio=4)/IP(src="1.2.3.4")/UDP(),
    Ether()/Dot1Q(vlan=2022)/IP()/UDP(),
    Ether()/Dot1Q(vlan=4023)/IP()/UDP(),
]

pkts_good_ipv4 = [
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(),
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.7.8", ttl=22)/UDP(),
    Ether()/Dot1Q(vlan=2025, prio=5)/IP(src="1.2.3.4", ttl=66)/UDP(),
]
pkts_bad_ipv4 = [
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.1", dst="1.3.3.7")/UDP(),
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.7.8", ttl=25)/UDP(),
    Ether()/Dot1Q(vlan=2025, prio=5)/IP(src="1.2.3.4", ttl=26)/UDP(),
]

pkts_good_ipv6 = [
    Ether()/Dot1Q(vlan=2025)/IPv6(src="dead::face", dst="dead::cafe")/UDP(),
    Ether()/Dot1Q(vlan=2025)/IPv6(src="face::cafe", dst="dead::dead", hlim=66)/UDP(),
    Ether()/Dot1Q(vlan=2025, prio=5)/IPv6(src="cafe::face", hlim=66)/UDP(),
]
pkts_bad_ipv6 = [
    Ether()/Dot1Q(vlan=2025)/IPv6(src="deac::face", dst="cead::cafe")/UDP(),
    Ether()/Dot1Q(vlan=2025)/IPv6(src="face::cafe", dst="dead::dead", hlim=36)/UDP(),
    Ether()/Dot1Q(vlan=2025, prio=5)/IPv6(src="cafe::facc", hlim=66)/UDP(),
]


def start_r2dtwo():
    return exec_bg("../r2dtwo match/match.ini")

def cleanup_ifaces():
    exec_fg("ip link del to_r2 type veth peer name r2rx")
    exec_fg("ip link del from_r2 type veth peer name r2tx")

def config_ifaces():
    ret = 0
    exec_fg("ip link del to_r2 type veth peer name r2rx")
    exec_fg("ip link del from_r2 type veth peer name r2tx")
    cmds = [
        "ip link add to_r2 type veth peer name r2rx",
        "ip link add from_r2 type veth peer name r2tx",
        "sysctl -w net.ipv6.conf.r2rx.disable_ipv6=1",
        "sysctl -w net.ipv6.conf.to_r2.disable_ipv6=1",
        "sysctl -w net.ipv6.conf.r2tx.disable_ipv6=1",
        "sysctl -w net.ipv6.conf.from_r2.disable_ipv6=1",
        "ip link set dev to_r2 up",
        "ip link set dev r2rx up",
        "ip link set dev from_r2 up",
        "ip link set dev r2tx up"
    ]
    for cmd in cmds:
        ret += exec_fg(cmd).returncode
    if ret > 0:
        print("Error(s) during interface config. Running without sudo?")
        exit(1)

def get_rxpktsnum(iface = "from_r2"):
    out = exec_fg(f"ip -j -p stats show dev {iface}")
    return int(json.loads(out.stdout)[1]["stats64"]["rx"]["packets"])

def test_ethernet():
    print("Test Ethernet maching...")
    start_r2dtwo()
    time.sleep(1)
    sendp(pkts_bad_eth + pkts_good_eth, verbose=0, iface="to_r2")
    if get_rxpktsnum() != len(pkts_good_eth):
        print("Failed")
        return 0
    return 1

def test_vlans():
    print("Test VLAN maching...")
    start_r2dtwo()
    time.sleep(1)
    sendp(pkts_bad_vlans + pkts_good_vlans, verbose=0, iface="to_r2")
    if get_rxpktsnum() != len(pkts_good_vlans):
        print("Failed")
        return 0
    return 1

def test_ipv4():
    print("Test IPv4 maching...")
    start_r2dtwo()
    time.sleep(1)
    sendp(pkts_bad_ipv4 + pkts_good_ipv4, verbose=0, iface="to_r2")
    if get_rxpktsnum() != len(pkts_good_ipv4):
        print("Failed")
        return 0
    return 1

def test_ipv6():
    print("Test VLAN maching...")
    start_r2dtwo()
    time.sleep(1)
    sendp(pkts_bad_ipv6 + pkts_good_ipv6, verbose=0, iface="to_r2")
    if get_rxpktsnum() != len(pkts_good_ipv6):
        print("Failed")
        return 0
    return 1

def main():
    print("R2DTWO match test")
    ret = 0
    tests = [test_vlans, test_ethernet, test_ipv4, test_ipv6]
    for test in tests:
        config_ifaces()
        ret += test()
        exec_fg("killall r2dtwo")
    print(f'All test completed, {ret}/{len(tests)} successfully')
    cleanup_ifaces()
    if ret != len(tests):
        exit(1)
    exit(0)

main()
