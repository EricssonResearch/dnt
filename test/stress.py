#!/usr/bin/python3

from utils import *
import signal
import time
import sys

# LONG_RUN_DURATION_SEC = 24 * 60 * 60
LONG_RUN_DURATION_SEC = 30

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
def ping_check_out_of_order(ping_output):
    seqs_str = [s for s in ping_output.splitlines() if "icmp_seq" in s]
    seqs = [int(s.split(" ")[4].split("=")[1]) for s in seqs_str]
    return all(s == s_prev + 1 for s_prev, s in zip(seqs, seqs[1:]))

def long_run():
    print(f"Run R2DTWOs for {LONG_RUN_DURATION_SEC} seconds...")
    exec_bg("../r2dtwo stress/r2br0.ini", out=OUT_NONE)
    exec_bg("../r2dtwo stress/r2br1.ini", out=OUT_NONE)
    time.sleep(1)
    ping = exec_bg("ping -I to_r2br0 10.0.0.2 -q -A", out=OUT_PIPE)
    time.sleep(LONG_RUN_DURATION_SEC)
    ping.send_signal(signal.SIGINT)
    ping_out = str(ping.communicate()[0])
    ping_out = ping_out.split('\n')
    for line in ping_out:
        if "transmitted" in line and "received" in line:
            params = line.split(" ")
            sent, recvd = params[0], params[3]
            if sent != recvd:
                print(line)
                return 0
    return 1


def main():
    print("R2DTWO POF test")
    create_ifaces()
    config_ifaces()
    if len(sys.argv) == 2 and sys.argv[1] == "--debug":
        exit(1)
    ret = 0
    tests = [long_run]
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
