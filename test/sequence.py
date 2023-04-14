#!/usr/bin/python3

from utils import *
import shlex
import time

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

    # accept local ARP on listener uni
    ret += exec_fg(f"sysctl -w net.ipv4.conf.to_r2br1.accept_local=1").returncode
    ret += exec_fg("ip addr flush dev to_r2br0").returncode
    ret += exec_fg("ip addr flush dev to_r2br1").returncode
    ret += exec_fg("ip addr add 10.0.0.1/24 dev to_r2br0").returncode
    ret += exec_fg("ip addr add 10.0.0.2/24 dev to_r2br1").returncode
    if ret > 0:
        print("Error(s) during interface config. Running without sudo?")
        exit(1)

def ping():
    try:
        print("Test simple ping 1k packets...")
        exec_bg("../r2dtwo sequence/r2br0.ini")
        exec_bg("../r2dtwo sequence/r2br1.ini")
        time.sleep(1)

        num_pings = 1000
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.0001 -c {num_pings}", timeout=5.0)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0

        print("Test simple ping 70k packets...")
        num_pings = 70000
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.0001 -c {num_pings}", timeout=10.0)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1

def reset():
    try:
        print("Test sequence recovery reset...")
        r2 = exec_bg("../r2dtwo sequence/r2br0.ini")
        exec_bg("../r2dtwo sequence/r2br1.ini")
        time.sleep(1)

        num_pings = 10
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.0001 -c {num_pings}", timeout=5.0)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0

        r2.terminate()
        # Trigger the recovery reset
        time.sleep(5)
        exec_bg("../r2dtwo sequence/r2br0.ini")
        time.sleep(1)

        num_pings = 10
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.0001 -c {num_pings}", timeout=5.0)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
    except:
        return 0
    return 1



def main():
    print("R2DTWO match test")
    create_ifaces()
    config_ifaces()
    ret = 0
    tests = [ping, reset]
    for test in tests:
        ret += test()
        exec_fg("killall r2dtwo")
    print(f'All test completed, {ret}/{len(tests)} successfully')
    cleanup_ifaces()
    if ret != len(tests):
        exit(1)
    exit(0)

if __name__ == "__main__":
    main()
