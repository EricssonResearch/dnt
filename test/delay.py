#!/usr/bin/python3

from utils import *
import time
import sys

stdouts = { }

nics = [
    ["to_r2br0", "r2br0_uni"],
    ["to_r2br1", "r2br1_uni"],
    ["r2br0_nni0", "r2br1_nni0"],
    ["r2br0_nni1", "r2br1_nni1"],
]

def create_ifaces():
    for nicpair in nics:
        exec_fg(f"ip link add {nicpair[0]} type veth peer name {nicpair[1]}")

def cleanup_ifaces():
    for nicpair in nics:
        exec_fg(f"ip link del {nicpair[0]} type veth peer name {nicpair[1]}")

def config_ifaces():
    ret = 0
    for nic in sum(nics, []):
        ret += exec_fg(f"sysctl -w net.ipv6.conf.{nic}.disable_ipv6=1").returncode
        ret += exec_fg(f"ip link set dev {nic} up").returncode

    # up loopback in case its running in namespace
    ret += exec_fg(f"ip link set dev lo up").returncode
    # accept local ARP on listener uni
    ret += exec_fg(f"sysctl -w net.ipv4.conf.to_r2br1.accept_local=1").returncode
    ret += exec_fg("ip addr flush dev to_r2br0").returncode
    ret += exec_fg("ip addr flush dev to_r2br1").returncode
    ret += exec_fg("ip addr add 10.0.0.1/24 dev to_r2br0").returncode
    ret += exec_fg("ip addr add 10.0.0.2/24 dev to_r2br1").returncode
    if ret > 0:
        print("Error(s) during interface config. Running without sudo?")
        exit(1)

# Tell if ping ICMP sequences in order (true) or not (false)
def ping_check_time(ping_output):
    times_str = [s for s in ping_output.splitlines() if "icmp_seq" in s]
    times_f = [float(s.split(" ")[6].split("=")[1]) for s in times_str]
    res_bool = [(f > 99 and f<101)  for f in times_f]
    return all(res_bool)


def delay_burst():
    try:
        print("Delay test with back-to-back packets...")
        exec_bg("../r2dtwo delay/r2br0.ini")
        exec_bg("../r2dtwo delay/r2br1.ini")
        time.sleep(1)
        num_pings = 10
        exec_fg(f"ping -I to_r2br0 10.0.0.2 -c 1")  # first ping has triple delay because of ARP
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.05 -c {num_pings}")
        #print(pingcmd.stdout)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_time(pingcmd.stdout) == False:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1


def delay_single():
    try:
        print("Delay test with single packets...")
        exec_bg("../r2dtwo delay/r2br0.ini")
        exec_bg("../r2dtwo delay/r2br1.ini")
        time.sleep(1)
        num_pings = 5
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -c {num_pings}")
        #print(pingcmd.stdout)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_time(pingcmd.stdout) == False:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1

def main():
    print("R2DTWO delay tests")
    create_ifaces()
    config_ifaces()
    if len(sys.argv) == 2 and sys.argv[1] == "--debug":
        exit(1)
    ret = 0
    tests = [delay_burst, delay_single]
    for test in tests:
        ret += test()
        exec_fg("killall r2dtwo")
    print(f'All test completed, {ret}/{len(tests)} successfully')
    # exit(0)
    cleanup_ifaces()
    if ret != len(tests):
        exit(1)
    exit(0)

if __name__ == "__main__":
    main()
