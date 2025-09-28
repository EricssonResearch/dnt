#!/usr/bin/python3

from scapy.layers.l2 import Ether, Dot1Q, Dot1AD
from scapy.layers.inet import IP, UDP
from scapy.contrib.mpls import MPLS
from scapy.layers.inet6 import IPv6
from scapy.all import sendp
from utils import *
import json
import time


pkts_good_eth = [
    Ether(dst="aa:bb:cc:aa:bb:cc")/Dot1Q(vlan=2025)/IP()/UDP(), # s00
    Ether(dst="aa:bb:cc:ff:ff:ff", src="dd:ee:ff:dd:ee:ff")/Dot1Q(vlan=2025)/IP()/UDP(), # s01
    Ether(dst="aa:bb:cc:ff:ff:ff", src="dd:ee:ff:dd:00:00")/Dot1Q(vlan=2025)/IP()/UDP(), # s01
]
pkts_bad_eth = [
    Ether(dst="ab:bb:cc:aa:bb:cd")/Dot1Q(vlan=2025)/IP()/UDP(), # s00 but wrong dst
    Ether(dst="aa:bb:cc:ff:ff:ff", src="dd:ee:ff:00:00:00")/Dot1Q(vlan=2025)/IP()/UDP(), # s01 but wrong src
]

pkts_good_vlans = [
    Ether()/Dot1Q(vlan=2023)/IP()/UDP(), # s10
    Ether()/Dot1Q(vlan=1111, prio=5)/IP(src="1.2.3.4")/UDP(), # s11
    Ether()/Dot1Q(vlan=99, prio=2)/IP(src="1.2.3.4")/UDP(), # s12
    Ether()/Dot1Q(vlan=66, prio=2)/IP(src="1.2.3.4")/UDP(), # s12
]
pkts_bad_vlans = [
    Ether()/Dot1Q(vlan=1111, prio=4)/IP(src="1.2.3.4")/UDP(), # s11 but wrong prio
    Ether()/Dot1Q(vlan=2022)/IP()/UDP(), # s10 but wrong vlan
    Ether()/Dot1Q(vlan=4023)/IP()/UDP(), # s10 but wrong vlan
]

pkts_good_ipv4 = [
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(), # s21
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.7.77")/UDP(), # s21
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.8.8", ttl=22)/UDP(), # s22
    Ether()/Dot1Q(vlan=2025, prio=5)/IP(src="1.2.3.4", ttl=66)/UDP(), # s20
]
pkts_bad_ipv4 = [
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.1", dst="1.3.3.7")/UDP(), # neither s21 nor s22
    Ether()/Dot1Q(vlan=2025)/IP(src="9.9.9.9", dst="7.7.8.8", ttl=25)/UDP(), # s22 but wrong ttl
    Ether()/Dot1Q(vlan=2025, prio=5)/IP(src="1.2.3.4", ttl=26)/UDP(), # s20 but wrong ttl
]

pkts_good_ipv6 = [
    Ether()/Dot1Q(vlan=2025)/IPv6(src="dead:face::face", dst="dead:cafe::cafe")/UDP(), # s31
    Ether()/Dot1Q(vlan=2025)/IPv6(src="dead:face::dead", dst="dead:cafe::dead")/UDP(), # s31
    Ether()/Dot1Q(vlan=2025)/IPv6(src="face::cafe", dst="dead::dead", hlim=66)/UDP(), # s32
    Ether()/Dot1Q(vlan=2025, prio=5)/IPv6(src="cafe::face", hlim=66)/UDP(), # s30
]
pkts_bad_ipv6 = [
    Ether()/Dot1Q(vlan=2025)/IPv6(src="deac:face::", dst="dead:cafe::")/UDP(), # s31 but wrong src
    Ether()/Dot1Q(vlan=2025)/IPv6(src="dead:face::", dst="cead:cafe::")/UDP(), # s31 but wrong dst
    Ether()/Dot1Q(vlan=2025)/IPv6(src="face::cafe", dst="dead::dead", hlim=36)/UDP(), # s32 but wrong hlim
    Ether()/Dot1Q(vlan=2025, prio=5)/IPv6(src="cafe::facc", hlim=66)/UDP(), # s31 but wrong src
]

pkts_good_stack = [
    Ether(dst="aa:bb:fc:fa:bb:cc")/Dot1Q(vlan=2065)/Dot1AD(vlan=3999)/IP(src="2.2.2.2",dst="6.6.6.6")/UDP()/MPLS(label=4444)/IPv6(src="dead::face", dst="dead::cafe"), # s40
    Ether(dst="aa:bb:fc:fa:bb:cc")/Dot1Q(vlan=2065)/Dot1AD(vlan=3999)/IP(src="2.2.2.2",dst="6.6.6.6")/UDP()/MPLS(label=1048575)/Dot1Q(vlan=33)/IPv6(src="dead::face", dst="dead::cafe"), # s41
]
pkts_bad_stack = [
    Ether(dst="aa:bb:fc:fa:bb:cc")/Dot1Q(vlan=2065)/Dot1AD(vlan=3999)/IP(src="2.2.2.2",dst="6.6.6.6")/UDP()/MPLS(label=4445)/IPv6(src="dead::face", dst="dead::cafe"), # s40 but wrong label
    Ether(dst="aa:bb:fc:fa:bb:cc")/Dot1Q(vlan=2065)/Dot1AD(vlan=3999)/IP(src="2.2.2.2",dst="6.6.6.6")/UDP()/MPLS(label=1048575)/Dot1Q(vlan=31)/IPv6(src="dead::face", dst="dead::cafe"), # s41 but wrong vlan_2
    Ether(dst="aa:bb:fc:fa:bb:cc")/Dot1Q(vlan=2065)/Dot1Q(vlan=3999)/IP(src="2.2.2.2",dst="6.6.6.6")/UDP()/MPLS(label=4444)/IPv6(src="dead::face", dst="dead::cafe"), # s40 but cvlan instead of svlan
]

pkts_good_neg = [
    Ether()/Dot1Q(vlan=2025)/IPv6(src="beef:dead:face::face", dst="cafe::cafe")/UDP(), # s50
    Ether()/Dot1Q(vlan=2025)/IPv6(src="beef:dead:face::face", dst="cead:cafe::cafe")/UDP(), # s50
]
pkts_bad_neg = [
    Ether()/Dot1Q(vlan=2025)/IPv6(src="beef:dead:face::face", dst="dead:cafe::cafe")/UDP(), # s50 but wrong dst
]

def start_r2dtwo():
    return exec_bg("../r2dtwo match/match.ini")

def cleanup_ifaces():
    exec_fg("ip link del to_r2 type veth peer name r2rx")
    exec_fg("ip link del from_r2 type veth peer name r2tx")

def config_ifaces():
    ret = 0
    cleanup_ifaces()
    # must disable ipv6 to get rid of the neighbor discovery packets
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

"""
    returns differential value of received packets on @iface since the last invocation
"""
rxpktsnum = {}
def get_rxpktsnum(iface = "from_r2"):
    out = exec_fg(f"ip -j -p stats show group link dev {iface}")
    num = int(json.loads(out.stdout)[0]["stats64"]["rx"]["packets"])
    if iface in rxpktsnum:
        prevnum = rxpktsnum[iface]
        rxpktsnum[iface] = num
        return num - prevnum
    else:
        rxpktsnum[iface] = num
        return num

def run_test(name, goods, bads):
    print(f"Test {name} matching...", end=" ")
    okay = 1
    sendp(goods, verbose=0, iface="to_r2")
    pktnum = get_rxpktsnum()
    if pktnum != len(goods):
        print(f"goods {pktnum}/{len(goods)}", end=" ")
        okay = 0
    sendp(bads, verbose=0, iface="to_r2")
    pktnum = get_rxpktsnum()
    if pktnum != 0:
        print(f"bads {pktnum}", end=" ")
        okay = 0
    return okay


def main():
    print("R2DTWO match test")
    passed = 0
    tests = [
            ["Ethernet", pkts_good_eth, pkts_bad_eth],
            ["VLAN", pkts_good_vlans, pkts_bad_vlans],
            ["IPv4", pkts_good_ipv4, pkts_bad_ipv4],
            ["IPv6", pkts_good_ipv6, pkts_bad_ipv6],
            ["Large stack", pkts_good_stack, pkts_bad_stack],
            ["Negative", pkts_good_neg, pkts_bad_neg],
            ]
    config_ifaces()
    start_r2dtwo()
    time.sleep(1)
    for test in tests:
        result = run_test(test[0], test[1], test[2])
        passed += result
        if result == 1:
            print("✔")
        else:
            print("✘")
    print(f'All test completed, {passed}/{len(tests)} successfully')
    exec_fg("killall r2dtwo")
    cleanup_ifaces()
    if passed != len(tests):
        exit(1)
    exit(0)

main()
