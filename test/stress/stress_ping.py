#!/usr/bin/python3

from socket import AF_INET, SOCK_STREAM, SocketType, socket
from mininet.net import Mininet
from threading import Thread
from mininet.cli import CLI
from utils import *
import signal
import time
import sys

# LONG_RUN_DURATION_SEC = 10 * 60 * 60
LONG_RUN_DURATION_SEC = 30

# Tell if ping ICMP sequences in order (true) or not (false)
def ping_check_out_of_order(ping_output):
    seqs_str = [s for s in ping_output.splitlines() if "icmp_seq" in s]
    seqs = [int(s.split(" ")[4].split("=")[1]) for s in seqs_str]
    return all(s == s_prev + 1 for s_prev, s in zip(seqs, seqs[1:]))

def long_run(net, debug : bool):
    t, l, a, b = [net.get(n) for n in ["t", "l", "a", "b"]]
    switch_netns("a")
    # rtwo1 = exec_bg(f"screen -S r1 -d -m env -i gdb -ex=r --args ../dnt stress/a.ini")
    rtwo1 = exec_bg(f"../dnt -vALL:NONE stress/a.ini")
    switch_netns("b")
    # rtwo2 = exec_bg(f"screen -S r2 -d -m env -i gdb -ex=r --args ../dnt stress/b.ini")
    rtwo2 = exec_bg(f"../dnt -vALL:NONE stress/b.ini")
    time.sleep(2)
    if debug:
        print("Debug mode. Press Ctrl+D or Ctrl+C to exit...")
        CLI(net)
        # For debug mode, we done
        return 1
    switch_netns("t")
    ping = exec_bg("ping 192.168.1.2 -q -A", out=OUT_PIPE)
    switch_netns("a")
    time.sleep(LONG_RUN_DURATION_SEC)

    ping.send_signal(signal.SIGINT)
    rtwo1.terminate()
    rtwo2.terminate()
    ping_out = str(ping.communicate()[0])
    ping_out = ping_out.split('\n')
    for line in ping_out:
        if "transmitted" in line and "received" in line:
            print(line)
            params = line.split(" ")
            sent, recvd = int(params[0]), int(params[3])
            if abs(sent - recvd) >= 2:
                return 0
    return 1

def create_net():
    net = Mininet(autoStaticArp=True)
    t = net.addHost("t", ip='192.168.1.1/24')
    l = net.addHost("l", ip='192.168.1.2/24')
    a = net.addHost("a", ip=None)
    b = net.addHost("b", ip=None)
    net.addLink(t, a, intfName1='eth0', intfName2='eth0')
    net.addLink(l, b, intfName1='eth0', intfName2='eth0')
    net.addLink(a, b, intfName1='ab0', intfName2='ba0')
    net.addLink(a, b, intfName1='ab1', intfName2='ba1')
    net.build()
    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")
    a.cmd("sysctl -w net.ipv4.ip_forward=1")
    b.cmd("sysctl -w net.ipv4.ip_forward=1")
    a.cmd("ip a a 10.0.0.1/32 dev lo")
    b.cmd("ip a a 10.0.0.2/32 dev lo")
    a.cmd("ip a a 11.0.0.1/24 dev ab0")
    a.cmd("ip a a 22.0.0.1/24 dev ab1")
    b.cmd("ip a a 11.0.0.2/24 dev ba0")
    b.cmd("ip a a 22.0.0.2/24 dev ba1")
    a.cmd("ip r a default via 11.0.0.2")
    b.cmd("ip r a default via 11.0.0.1")
    return net

def main():
    debug = False
    all_ok = False
    print("DNT stress test")
    if len(sys.argv) == 2 and sys.argv[1] == "--debug":
        debug = True
    try:
        net = create_net()
        result = long_run(net, debug)
        if result == 0:
            print("✘")
            all_ok = False
        else:
            print("✔")
            all_ok = True
        exec_fg("killall dnt")
    except KeyboardInterrupt:
        print(" Interrupted, cleanup...")
        exec_fg("killall ping")
        exec_fg("killall dnt")
        exec_fg("killall gdb")
        net.stop()
        exit(1)
    finally:
        exec_fg("killall ping")
        exec_fg("killall dnt")
        exec_fg("killall gdb")
        net.stop()
        if all_ok:
            exit(0)
        exit(1)

if __name__ == "__main__":
    main()
