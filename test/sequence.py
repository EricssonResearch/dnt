#!/usr/bin/python3

from utils import *
import signal
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
        print("Test simple ping 1k packets...", end=" ")
        exec_bg("../r2dtwo sequence/r2br0.ini")
        exec_bg("../r2dtwo sequence/r2br1.ini")
        time.sleep(1)

        num_pings = 1000
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -i 0.0001 -c {num_pings}", timeout=5.0)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0

        print("Test simple ping 70k packets...", end=" ")
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
        print("Test sequence recovery reset...", end=" ")
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

def flapping():
    try:
        exec_bg("../r2dtwo sequence/r2br0.ini")
        time.sleep(1)

        num_pings = 500
        ping = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.01 -c {num_pings} -W 10", out=OUT_PIPE)
        for cmd in ["down", "up"] * 5:
            time.sleep(0.1)
            exec_fg(f"ip link set dev r2br1_nni0 {cmd}")
        for cmd in ["down", "up"] * 5:
            time.sleep(0.1)
            exec_fg(f"ip link set dev r2br1_nni1 {cmd}")
        ping_out = str(ping.communicate()[0])
        if f"duplicates" in ping_out:
            return False
    except:
        return True
    return True

def flapping_bad():
    print("Test path flapping without recovery...", end=" ")
    exec_bg("../r2dtwo sequence/r2br1_norcvy.ini")
    if flapping() == True:
        return 0
    return 1

def flapping_good():
    print("Test path flapping with recovery...", end=" ")
    exec_bg("../r2dtwo sequence/r2br1.ini")
    if flapping() == False:
        return 0
    return 1

def resetonly_bad():
    print("Test reset flag with reset flag unaware recovery...", end=" ")
    try:
        br0 = exec_bg("../r2dtwo sequence/r2br0_resetonly.ini")
        exec_bg("../r2dtwo sequence/r2br1.ini")
        time.sleep(1)
        num_pings = 20
        pingcmd = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.1 -c {num_pings}", out=OUT_PIPE)
        time.sleep(1)
        # Reset the seq generators of r2br0 instance with SIGUSR1
        br0.send_signal(signal.SIGUSR1)
        ping_output = str(pingcmd.communicate()[0])
        if ping_output and f", {num_pings} received," not in ping_output:
            return 1
    except:
        return 0
    return 0

def resetonly_good():
    print("Test reset flag with reset flag aware recovery...", end=" ")
    try:
        br0 = exec_bg("../r2dtwo sequence/r2br0_resetonly.ini")
        exec_bg("../r2dtwo sequence/r2br1_resetonly.ini")
        time.sleep(1)
        num_pings = 20
        pingcmd = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.1 -c {num_pings}", out=OUT_PIPE)
        time.sleep(1)
        # Reset the seq generators of r2br0 instance with SIGUSR1
        br0.send_signal(signal.SIGUSR1)
        ping_output = str(pingcmd.communicate()[0])
        if ping_output and f", {num_pings} received," in ping_output:
            return 1
    except:
        return 0
    return 0

def seamless_bad():
    print("Test init+reset flag with init/reset flag unaware recovery...", end=" ")
    try:
        br0 = exec_bg("../r2dtwo sequence/r2br0_init.ini")
        exec_bg("../r2dtwo sequence/r2br1.ini")
        time.sleep(1)
        num_pings = 20
        pingcmd = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.1 -c {num_pings}", out=OUT_PIPE)
        time.sleep(1)
        # Reset the seq generators of r2br0 instance with SIGUSR1
        br0.send_signal(signal.SIGUSR1)
        ping_output = str(pingcmd.communicate()[0])
        if ping_output and f", {num_pings} received," not in ping_output:
            return 1
    except:
        return 0
    return 0

def seamless_good():
    print("Test init+reset flag with seamless recovery...", end=" ")
    try:
        br0 = exec_bg("../r2dtwo sequence/r2br0_init.ini")
        tshark = exec_bg("tshark -l -O 'ieee8021cb' -i r2br0_nni0", out=OUT_PIPE)
        exec_bg("../r2dtwo sequence/r2br1_init.ini")
        time.sleep(1)
        num_pings = 30
        pingcmd = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.1 -c {num_pings}", out=OUT_PIPE)
        time.sleep(1)
        # Reset the seq generators of r2br0 instance with SIGUSR1
        br0.send_signal(signal.SIGUSR1)
        ping_output = str(pingcmd.communicate()[0])
        tshark.kill()
        tshark_output = str(tshark.communicate()[0])
        if ping_output and f", {num_pings} received," in ping_output:
            # From linear seq space we must have to see a large seq
            if "SEQ: 65533" in tshark_output:
                return 1
    except:
        return 0
    return 0


def main():
    print("R2DTWO sequence generator and recovery test\n")
    create_ifaces()
    config_ifaces()
    if len(sys.argv) == 2 and "debug" in sys.argv[1]:
        exit(0)
    ret = 0
    tests = [ping, reset, flapping_bad, flapping_good, resetonly_good, resetonly_bad, seamless_good, seamless_bad]
    for test in tests:
        result = test()
        ret += result
        if result == 1:
            print("✔")
        else:
            print("✘")
        exec_fg("killall r2dtwo")
    print(f'All test completed, {ret}/{len(tests)} successfully')
    cleanup_ifaces()
    if ret != len(tests):
        exit(1)
    exit(0)

if __name__ == "__main__":
    main()
