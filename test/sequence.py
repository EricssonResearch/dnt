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

def is_recent_tshark():
    '''
    Return 1 if tshark using new R-tag field name
    and return 0 if old name supported.
    Old: >= 4.0, New: >= 4.2
    '''
    ret = exec_fg("tshark -G")
    if ret.stdout:
        if "rtag.seqno" in ret.stdout:
            return 1
    return 0

def create_ifaces():
    for nicpair in nics:
        exec_fg(f"ip link add {nicpair[0]} type veth peer name {nicpair[1]}")

def cleanup_ifaces():
    print("Cleanup interfaces")
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
        if is_recent_tshark():
            tsharkcmd = "tshark -l -O 'ieee8021cb' -T fields -e rtag.seqno -i r2br0_nni0"
            seq_base = 10 #new tshark using decimal print format
        else:
            tsharkcmd = "tshark -l -O 'ieee8021cb' -e ieee8021cb.seq -T fields -i r2br0_nni0"
            seq_base = 16 #old tshark using hexa print format
        br0 = exec_bg("../r2dtwo sequence/r2br0_init.ini")
        tshark = exec_bg(tsharkcmd, out=OUT_PIPE)
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
            for seq in tshark_output.split('\n'):
                if int(seq, base=seq_base) == 65533:
                    return 1
    except:
        return 0
    return 0

def match_good():
    print("Test match recovery...", end=" ")
    try:
        exec_bg("../r2dtwo sequence/r2br0_match.ini", out=OUT_NONE)
        exec_bg("../r2dtwo sequence/r2br1_match.ini", out=OUT_NONE)
        time.sleep(1)
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -A -c 5 -W 5", timeout=3.0)
        num_pings = 500
        pingcmd = exec_fg(f"ping -I to_r2br0 10.0.0.2 -A -c {num_pings} -W 5", timeout=3.0)
        if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
            print(pingcmd.stdout)
            return 0
    except:
        return 0
    return 1

def match_path_fail():
    print("Test match recovery with path failure...", end=" ")
    try:
        exec_bg("../r2dtwo sequence/r2br0_match.ini", out=OUT_NONE)
        exec_bg("../r2dtwo sequence/r2br1_match.ini", out=OUT_NONE)
        time.sleep(1)
        num_pings = 30
        pingcmd = exec_bg(f"ping -I to_r2br0 10.0.0.2 -i 0.1 -c {num_pings}", out=OUT_PIPE)
        time.sleep(1)
        exec_fg("ip link set dev r2br0_nni0 down")
        exec_fg("ip link set dev r2br0_nni1 down")
        time.sleep(1)
        exec_fg("ip link set dev r2br0_nni0 up")
        exec_fg("ip link set dev r2br0_nni1 up")
        time.sleep(2)
        ping_output = str(pingcmd.communicate()[0])
        if ping_output and "30 packets transmitted, 20 received" not in ping_output:
            print(pingcmd.stdout)
            return 0
    except:
        return 0
    return 1

def main():
    print("R2DTWO sequence generator and recovery test\n")
    try:
        create_ifaces()
        config_ifaces()
        if len(sys.argv) == 2 and "debug" in sys.argv[1]:
            print("Press Ctrl+C to cleanup the test network...")
            while True:
                time.sleep(1000)
        ret = 0
        tests = [ping, reset, flapping_bad, flapping_good, resetonly_good, resetonly_bad, seamless_good, seamless_bad, match_good, match_path_fail]
        for test in tests:
            result = test()
            ret += result
            if result == 1:
                print("✔")
            else:
                print("✘")
            exec_fg("killall r2dtwo")
            time.sleep(0.5)
        print(f'All test completed, {ret}/{len(tests)} successfully')
        cleanup_ifaces()
        if ret != len(tests):
            exit(1)
        exit(0)
    except KeyboardInterrupt:
        cleanup_ifaces()
        exit(1)


if __name__ == "__main__":
    main()
