#!/usr/bin/python

from mininet.net import Mininet
from mininet.node import Host, Node
from mininet.cli import CLI
from mininet.log import setLogLevel, info
from functools import partial
from utils import *
import time
import sys
import re

PATH_DELAY_MS = 30
PING_INTERVAL_SEC = 0.05
PING_NUM = 10

def setup_network():
    """
    For basic SRv6 testing.
               r3
              /   \
    t1 ---- r2-----r4 ---- l5

    ipv6 test command: t1 ping6 fd05:a1fa::5  and t1 ping6 fd05:a1fa::5 -Q 0xc0 -F 1234
    ipv4 test command: t1 ping 10.0.5.1   and  t1 ping  -Q 0xc0 10.0.5.1
    TSN  test command: t1 ping 10.10.0.2  and  t1 ping  -Q 0xc0 10.10.0.2

    """

    net = Mininet(waitConnected=True)

    info('*** Adding hosts\n')
    hostnames = ['t1', 'r2', 'r3', 'r4', 'l5']
    t1, r2, r3, r4, l5 = [net.addHost(n, ip=None) for n in hostnames]

    for node in [t1, r2, r3, r4, l5]:
        node.cmd("sysctl -w net.ipv6.conf.all.seg6_enabled=1")
        node.cmd("sysctl -w net.ipv6.conf.default.seg6_enabled=1")
        node.cmd("sysctl -w net.ipv6.conf.all.forwarding=1")

    info('*** Creating links\n')
    net.addLink(t1, r2, intfName1='eth_t1r2', intfName2='eth_r2t1')
    net.addLink(r2, r3, intfName1='eth_r2r3', intfName2='eth_r3r2')
    net.addLink(r2, r4, intfName1='eth_r2r4', intfName2='eth_r4r2')
    net.addLink(r3, r4, intfName1='eth_r3r4', intfName2='eth_r4r3')
    net.addLink(r4, l5, intfName1='eth_r4l5', intfName2='eth_l5r4')

    net.build()

    # add vrf interfaces
    r2.cmd("ip link add name vrf1 type vrf table 10")
    r2.cmd("ip link set vrf1 mtu 2000 up")
    r2.cmd("ip -6 rule del priority 1000")
    r2.cmd("ip -6 rule add type blackhole l3mdev to fd00:be1a:0:2::/64 priority 1000")   # prevent loop
    r2.cmd("ip -6 rule add table local priority 2000")
    r2.cmd("ip -6 addr add fd00:be12::2/64 dev vrf1")

    r4.cmd("ip link add name vrf1 type vrf table 10")
    r4.cmd("ip link set vrf1 mtu 2000 up")
    r4.cmd("ip -6 rule del priority 1000")
    r4.cmd("ip -6 rule add type blackhole l3mdev to fd00:be1a:0:4::/64 priority 1000")   # prevent loop
    r4.cmd("ip -6 rule add table local priority 2000")
    r4.cmd("ip -6 addr add fd00:be12::4/64 dev vrf1")


    # add dummy loopback interfaces for local SIDs (loopback interfaces do not work with SRv6)
    r2.cmd("ip link add name sr0 type dummy")
    r2.cmd("ip a a fd12:fade::0/64 dev sr0")
    r3.cmd("ip link add name sr0 type dummy")
    r3.cmd("ip a a fd13:fade::0/64 dev sr0")
    r4.cmd("ip link add name sr0 type dummy")
    r4.cmd("ip a a fd14:fade::0/64 dev sr0")

    # add veth interfaces (not needed for TSN over SRv6)
    r2.cmd("ip link add veth0 type veth peer name veth1")
    r2.cmd("ip link set veth0 mtu 2000 up")
    r2.cmd("ip link set veth1 mtu 2000 up")
    r4.cmd("ip link add veth0 type veth peer name veth1")
    r4.cmd("ip link set veth0 mtu 2000 up")
    r4.cmd("ip link set veth1 mtu 2000 up")

    info('*** Adding IPv6 addresses\n')
    t1.cmd("ip a a fd01:a1fa::1/64 dev eth_t1r2")
    r2.cmd("ip a a fd01:a1fa::2/64 dev eth_r2t1")
    r2.cmd("ip a a fd02:a1fa::2/64 dev eth_r2r3")
    r2.cmd("ip a a fd03:a1fa::2/64 dev eth_r2r4")
    r3.cmd("ip a a fd02:a1fa::3/64 dev eth_r3r2")
    r3.cmd("ip a a fd04:a1fa::3/64 dev eth_r3r4")
    r4.cmd("ip a a fd03:a1fa::4/64 dev eth_r4r2")
    r4.cmd("ip a a fd04:a1fa::4/64 dev eth_r4r3")
    r4.cmd("ip a a fd05:a1fa::4/64 dev eth_r4l5")
    l5.cmd("ip a a fd05:a1fa::5/64 dev eth_l5r4")

    # IPv4 addresses and routes for IPv4 UNI
    t1.cmd("ip a a 10.0.1.1/24 dev eth_t1r2")
    r2.cmd("ip a a 10.0.1.2/24 dev eth_r2t1")
    r4.cmd("ip a a 10.0.5.2/24 dev eth_r4l5")
    l5.cmd("ip a a 10.0.5.1/24 dev eth_l5r4")
    t1.cmd("ip r add 0.0.0.0/0 via 10.0.1.2 dev eth_t1r2")  # def. gw is r2
    l5.cmd("ip r add 0.0.0.0/0 via 10.0.5.2 dev eth_l5r4")  # def. gw is r4

    # VLAN interfaces for TSN UNI
    t1.cmd("ip link add link eth_t1r2 name eth_t1r2.10 type vlan id 10 egress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7")
    t1.cmd("ip link set eth_t1r2.10 up")
    t1.cmd("ip a a 10.10.0.1/24 dev eth_t1r2.10")
    l5.cmd("ip link add link eth_l5r4 name eth_l5r4.10 type vlan id 10 egress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7")
    l5.cmd("ip link set eth_l5r4.10 up")
    l5.cmd("ip a a 10.10.0.2/24 dev eth_l5r4.10")
    # edit skb priority on hosts to map DSCP 0xC0 traffic to PCP 6
    t1.cmd("tc qdisc add dev eth_t1r2.10 clsact")
    t1.cmd("tc filter add dev eth_t1r2.10 egress protocol ipv4 flower ip_tos 0xc0 action skbedit priority 6")
    l5.cmd("tc qdisc add dev eth_l5r4.10 clsact")
    l5.cmd("tc filter add dev eth_l5r4.10 egress protocol ipv4 flower ip_tos 0xc0 action skbedit priority 6")

    # routing entries for interface IPs
    t1.cmd("ip -6 r add ::/0 via fd01:a1fa::2 dev eth_t1r2")  # def. gw is r2
    r2.cmd("ip -6 r add ::/0 via fd03:a1fa::4 dev eth_r2r4")  # def. path is via r4
    r3.cmd("ip -6 r add fd05:a1fa::/64 via fd04:a1fa::4 dev eth_r3r4")
    r3.cmd("ip -6 r add fd01:a1fa::/64 via fd02:a1fa::2 dev eth_r3r2")
    r4.cmd("ip -6 r add ::/0 via fd03:a1fa::2 dev eth_r4r2")  # def. path is via r2
    l5.cmd("ip -6 r add ::/0 via fd05:a1fa::4 dev eth_l5r4")  # def. gw is r4

    # set up routes for SID locators
    r2.cmd("ip -6 route add fd13:fade::/64 via fd02:a1fa::3 dev eth_r2r3")
    r2.cmd("ip -6 route add fd14:fade::/64 via fd03:a1fa::4 dev eth_r2r4")
    r3.cmd("ip -6 route add fd12:fade::/64 via fd02:a1fa::2 dev eth_r3r2")
    r3.cmd("ip -6 route add fd14:fade::/64 via fd04:a1fa::4 dev eth_r3r4")
    r4.cmd("ip -6 route add fd12:fade::/64 via fd03:a1fa::2 dev eth_r4r2")
    r4.cmd("ip -6 route add fd13:fade::/64 via fd04:a1fa::3 dev eth_r4r3")


    info('*** Setting up SRv6 tunnels\n')
    # set up direct tunnel
    r2.cmd("tc qdisc add dev eth_r2t1 handle ffff: ingress")
    r2.cmd("tc filter add dev eth_r2t1 parent ffff: protocol ipv6 flower dst_ip fd05:a1fa::5 action mirred egress redirect dev veth0")
    r2.cmd("tc filter add dev eth_r2t1 parent ffff: protocol ip flower dst_ip 10.0.5.1 action mirred egress redirect dev veth0")

    r2.cmd("ip -6 route add fd00:be1a:0:4:0::/80 encap seg6 mode encap segs fd14:fade:0:0:1:: dev eth_r2r4")
    r2.cmd("ip -6 route add fd00:be1a:0:4:1::/80 encap seg6 mode encap segs fd13:fade::0,fd14:fade:0:0:1:: dev eth_r2r3")
    r4.cmd("ip -6 route add fd00:be1a:0:4::/64 dev vrf1")
    r4.cmd("ip -6 route add fd14:fade:0:0:1::/128 encap seg6local action End.DT6 table 254 dev vrf1")

    # set up reverse tunnel
    r4.cmd("tc qdisc add dev eth_r4l5 handle ffff: ingress")
    r4.cmd("tc filter add dev eth_r4l5 parent ffff: protocol ipv6 flower dst_ip fd01:a1fa::1 action mirred egress redirect dev veth0")
    r4.cmd("tc filter add dev eth_r4l5 parent ffff: protocol ip flower dst_ip 10.0.1.1 action mirred egress redirect dev veth0")

    r4.cmd("ip -6 route add fd00:be1a:0:2:0::/80 encap seg6 mode encap segs fd12:fade:0:0:1:: dev eth_r4r2")
    r4.cmd("ip -6 route add fd00:be1a:0:2:1::/80 encap seg6 mode encap segs fd13:fade::0,fd12:fade:0:0:1:: dev eth_r4r3")
    r2.cmd("ip -6 route add fd00:be1a:0:2::/64  dev vrf1")
    r2.cmd("ip -6 route add fd12:fade:0:0:1::/128 encap seg6local action End.DT6 table 254 dev vrf1")

    info('*** Starting network\n')
    net.start()

    return net

def start_r2dtwos(net, scenario, debug):
    # start r2DTWOs
    for n in ['r2', 'r4']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo srv6/{n}-{scenario}.cfg -v ALL:ALL")
        else:
            node.popen(f"../r2dtwo -of srv6/{n}-{scenario}.cfg -v PACKETTRACE:PACKET")    # in general this is enough for debug
            #node.popen(f"../r2dtwo -of srv6/{n}.cfg -v  ALL:ALL")             # but sometimes we need all logs...

def stop_r2dtwos():
        switch_netns()
        exec_fg("killall r2dtwo")

def stop_network(net):
    info('*** Stopping network')
    net.stop()

def check_log(logfile, pattern, size):
    # Define the pattern to search for
    pattern = re.compile(pattern)

    # Open the logfile for reading
    with open(logfile, 'r') as file:
        lines = file.readlines()

    # Iterate through each line in the logfile
    n=0
    for line in lines:
        if pattern.search(line):
            # Extract the value before s3
            match = re.search(pattern, line)
            if match:
                value_before_s3 = match.group(1)
                if value_before_s3 == str(size):
                    n=n+1
                    #print(f"Line matches the criteria: {line.strip()}")
                else:
                    print(f"Size mismatch: {size}: {line.strip()}")
    return n

def test_ipv6():
    pids={}
    retval=1
    try:
        print("Test SRv6 with IPv6 UNI ", end=" ")
        # start r2DTWOs
        for n in ['r2', 'r4']:
            node = net.get(n)
            p=node.popen(f"../r2dtwo -of srv6/{n}-ipv6.cfg -v PACKETTRACE:PACKET")    # in general this is enough for debug
            pids[n]=f"r2dtwo-{n}-ipv6-{p.pid}.log"

        time.sleep(2)
        num_pings = PING_NUM
        switch_netns("t1")

        print(" Background traffic...", end=" ")
        pingcmd = exec_fg(f"ping6 fd05:a1fa::5 -i {PING_INTERVAL_SEC} -c {num_pings}")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            retval=0
        print(" DetNet traffic...", end=" ")
        pingcmd = exec_fg(f"ping6 fd05:a1fa::5 -Q 0xc0 -F 1234 -i {PING_INTERVAL_SEC} -c {num_pings}")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            retval=0

    except Exception as e:
            print(e)
            stop_r2dtwos()
            return 0

    stop_r2dtwos()

    print(" packet sizes...", end=" ")
    #input("Press Enter to continue...")
    r=check_log(pids['r2'], r'if3\s+(\d+)\s+s3\s+\|ipv6\|ipv6\|payload\|', 144)
    if r!=PING_NUM:
        print(f"found {r} log lines instead of {PING_NUM}")
        retval=0
    r=check_log(pids['r4'], r'if3\s+(\d+)\s+s5\s+\|ipv6\|ipv6\|payload\|', 144)
    if r!=PING_NUM:
        print(f"found {r} log lines instead of {PING_NUM}")
        retval=0

    #clean up logfiles
    for n in ['r2', 'r4']:
        os.remove(pids[n])

    return retval

def test_ipv4():
    pids={}
    retval=1
    try:
        print("Test SRv6 with IPv4 UNI ", end=" ")
        # start r2DTWOs
        for n in ['r2', 'r4']:
            node = net.get(n)
            p=node.popen(f"../r2dtwo -of srv6/{n}-ipv4.cfg -v PACKETTRACE:PACKET")    # in general this is enough for debug
            pids[n]=f"r2dtwo-{n}-ipv4-{p.pid}.log"

        time.sleep(2)
        num_pings = PING_NUM
        switch_netns("t1")

        print(" Background traffic...", end=" ")
        pingcmd = exec_fg(f"ping 10.0.5.1 -i {PING_INTERVAL_SEC} -c {num_pings}")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            retval=0

        print(" DetNet traffic...", end=" ")
        pingcmd = exec_fg(f"ping 10.0.5.1 -Q 0xc0 -i {PING_INTERVAL_SEC} -c {num_pings}")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            retval=0

    except Exception as e:
            print(e)
            stop_r2dtwos()
            return 0

    stop_r2dtwos()

    print(" packet sizes...", end=" ")
    #input("Press Enter to continue...")
    r=check_log(pids['r2'], r'if3\s+(\d+)\s+s3\s+\|ipv6\|ipv4\|payload\|', 124)
    if r!=PING_NUM:
        print(f"found {r} log lines instead of {PING_NUM}")
        retval=0
    r=check_log(pids['r4'], r'if3\s+(\d+)\s+s5\s+\|ipv6\|ipv4\|payload\|', 124)
    if r!=PING_NUM:
        print(f"found {r} log lines instead of {PING_NUM}")
        retval=0

    #clean up logfiles
    for n in ['r2', 'r4']:
        os.remove(pids[n])

    return retval

def test_tsn():
    pids={}
    retval=1
    try:
        print("Test SRv6 with TSN  UNI ", end=" ")
        # start r2DTWOs
        for n in ['r2', 'r4']:
            node = net.get(n)
            p=node.popen(f"../r2dtwo -of srv6/{n}-tsn.cfg -v PACKETTRACE:PACKET")    # in general this is enough for debug
            pids[n]=f"r2dtwo-{n}-tsn-{p.pid}.log"

        time.sleep(2)
        num_pings = PING_NUM
        switch_netns("t1")

        print(" Background traffic...", end=" ")
        pingcmd = exec_fg(f"ping 10.10.0.2 -i {PING_INTERVAL_SEC} -c {num_pings}")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            retval=0
        print(" TSN    traffic...", end=" ")
        pingcmd = exec_fg(f"ping 10.10.0.2 -Q 0xc0 -i {PING_INTERVAL_SEC} -c {num_pings}")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            retval=0

    except Exception as e:
            print(e)
            stop_r2dtwos()
            return 0

    stop_r2dtwos()

    print(" packet sizes...", end=" ")
    #input("Press Enter to continue...")
    r=check_log(pids['r2'], r'if3\s+(\d+)\s+s3\s+\|ipv6\|eth\|payload\|', 142)
    if r!=PING_NUM:
        print(f"found {r} log lines instead of {PING_NUM}")
        retval=0
    r=check_log(pids['r4'], r'if3\s+(\d+)\s+s5\s+\|ipv6\|eth\|payload\|', 142)
    if r!=PING_NUM:
        print(f"found {r} log lines instead of {PING_NUM}")
        retval=0

    #clean up logfiles
    for n in ['r2', 'r4']:
        os.remove(pids[n])

    return retval

if __name__ == '__main__':
    debug = False
    scenario="ipv6"     # possible scenarios ipv6, ipv4 and tsn
    if len(sys.argv) >= 2 and sys.argv[1] == "debug":
        debug = True
        if(len(sys.argv) >= 3):
            scenario=sys.argv[2]

    setLogLevel('info')
    net = setup_network()

    if debug:
        print("R2DTWO SRv6 debug")
        info(f"*** Starting R2DTWOs, scenario {scenario}\n")
        #sstart_r2dtwos(net, debug)
        start_r2dtwos(net, scenario, False)
        CLI(net)
        print("Cleanup...")
        exec_fg("killall r2dtwo")

    else:
        print("R2DTWO SRv6 test")
        ret = 0
        tests = [test_ipv6, test_ipv4, test_tsn]
        for test in tests:
            result = test()
            ret += result
            if result == 1:
                print("✔")
            else:
                print("✘")
            exec_fg("killall r2dtwo")
        print(f'All test completed, {ret}/{len(tests)} successfully')

    stop_network(net)

    if ret == len(tests):
        exit(0)
    exit(1)
