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
def ping_check_out_of_order(ping_output):
    seqs_str = [s for s in ping_output.splitlines() if "icmp_seq" in s]
    seqs = [int(s.split(" ")[4].split("=")[1]) for s in seqs_str]
    return all(s == s_prev + 1 for s_prev, s in zip(seqs, seqs[1:]))


def no_out_of_order():
    try:
        print("Test POF with no out of order delivery...")
        exec_bg("../r2dtwo pof/r2br0.ini")
        exec_bg("../r2dtwo pof/r2br1.ini")
        time.sleep(1)
        exec_fg("ip link set dev r2br0_nni1 down")
        num_pings = 1000
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.005 -c {num_pings}")
        exec_fg("ip link set dev r2br0_nni1 up")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_out_of_order(pingcmd.stdout) != True:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1


def ofo_no_pof():
    try:
        print("Test out of order TSN delivery without POF...")
        exec_bg("../r2dtwo pof/r2br0.ini")
        exec_bg("../r2dtwo pof/r2br1_nopof.ini")
        time.sleep(1)
        num_pings = 1000
        exec_fg("tc qdisc add dev r2br0_nni1 root netem delay 30ms reorder 50% 50%")
        exec_fg("tc qdisc add dev r2br0_nni0 root netem delay 30ms reorder 50% 50%")
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.005 -c {num_pings}")
        exec_fg("tc qdisc del dev r2br0_nni1 root")
        exec_fg("tc qdisc del dev r2br0_nni0 root")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_out_of_order(pingcmd.stdout) == True:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1


def ofo_pof():
    try:
        print("Test out of order TSN delivery with POF...")
        exec_bg("../r2dtwo pof/r2br0.ini")
        exec_bg("../r2dtwo pof/r2br1.ini")
        time.sleep(1)
        num_pings = 1000
        exec_fg("tc qdisc add dev r2br0_nni1 root netem delay 30ms reorder 50% 50%")
        exec_fg("tc qdisc add dev r2br0_nni0 root netem delay 30ms reorder 50% 50%")
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.005 -c {num_pings}")
        exec_fg("tc qdisc del dev r2br0_nni1 root")
        exec_fg("tc qdisc del dev r2br0_nni0 root")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_out_of_order(pingcmd.stdout) != True:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1


def pof_reset():
    try:
        print("Test POF reset...")
        exec_bg("../r2dtwo pof/r2br0.ini")
        exec_bg("../r2dtwo pof/r2br1.ini")
        time.sleep(1)
        num_pings = 100
        exec_fg("tc qdisc add dev r2br0_nni1 root netem delay 30ms reorder 50% 50%")
        exec_fg("tc qdisc add dev r2br0_nni0 root netem delay 30ms reorder 50% 50%")
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.005 -c {num_pings}")
        time.sleep(3) # sleep more than POF reset (=1sec)
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.005 -c {num_pings}")
        exec_fg("tc qdisc del dev r2br0_nni1 root")
        exec_fg("tc qdisc del dev r2br0_nni0 root")
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_out_of_order(pingcmd.stdout) != True:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1

def ofo_pof_smallbuffer():
    try:
        print("Test out of order TSN delivery with small buffer=2 POF...")
        exec_bg("../r2dtwo pof/r2br0.ini")
        exec_bg("../r2dtwo pof/r2br1_smallbuf.ini")
        time.sleep(1)
        num_pings = 100
        exec_fg("tc qdisc add dev r2br0_nni1 root netem delay 30ms reorder 50% 50%")
        exec_fg("tc qdisc add dev r2br0_nni0 root netem delay 30ms reorder 50% 50%")
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.005 -c {num_pings}")
        exec_fg("tc qdisc del dev r2br0_nni1 root")
        exec_fg("tc qdisc del dev r2br0_nni0 root")
        if pingcmd.stdout and f", {num_pings} received," in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
        if ping_check_out_of_order(pingcmd.stdout) == True:
            print(pingcmd.stdout)
            return 0
    except:
            return 0
    return 1


# we have a fast path: 0ms delay and a slow one with 500ms delay.
# The r2br1's POF max delay set to 520ms.
# Traffic is ping, 1 packet in every 0.1sec (100ms) so the fast path aways 5 packets ahead
# After ping starts, we wait for 1 sec then switch down the fast path.
# We keep getting packets from slow path, but packet 11, 12, 13 etc. are 500 ms delayed.
# So we holding them in the buffer. After 0.5 sec (before max delay expire for packet 11!)
# the fast path returns and we accept packets on that again. But as packet 11 expires in POF,
# we can send all the in-seqence buffered packets inmediately!
# As a result, (since we dont have delay on the reverse direction) we must see a 5 packet 500ms burst
# in the output of the ping. If not, the test was not successful
def pof_burst():
    try:
        print("Test POF burst (faster path's failure+recover)...")
        exec_bg("../r2dtwo pof/r2br0.ini", OUT_NONE)
        exec_bg("../r2dtwo pof/r2br1.ini", OUT_NONE)
        time.sleep(1)

        num_pings = 40
        exec_fg("tc qdisc add dev r2br0_nni1 root netem delay 500ms")
        ping = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.1 -c {num_pings} -W 10", OUT_PIPE)
        time.sleep(1)
        exec_fg(f"ip link set dev r2br1_nni0 down")
        time.sleep(0.5)
        exec_fg(f"ip link set dev r2br1_nni0 up")

        # Notes: do not remove netem qdisc during the test!
        # It will drop some packets and send the remaining in a burst
        ping_out = str(ping.communicate()[0])
        exec_fg("tc qdisc del dev r2br0_nni1 root")
        if f"duplicates" in ping_out:
            return 0
        if ", 0% packet loss" not in ping_out:
            return 0
        # with good delays, 5 packets burst icmp_seq 11-15 observed
        for i in range(11, 16):
            if f"icmp_seq={i} ttl=64 time=500 ms" not in ping_out:
                return 0
    except:
        return 0
    return 1

def main():
    print("R2DTWO POF test")
    create_ifaces()
    config_ifaces()
    if len(sys.argv) == 2 and sys.argv[1] == "--debug":
        exit(1)
    ret = 0
    tests = [no_out_of_order, ofo_no_pof, ofo_pof, pof_reset, ofo_pof_smallbuffer, pof_burst]
    # tests = [pof_burst]
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
