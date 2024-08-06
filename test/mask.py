#!/usr/bin/python3


from socket import AF_INET, IPPROTO_UDP, SOCK_DGRAM, socket
from mininet.net import Mininet
from mininet.cli import CLI
from select import *
from utils import *
import time
import sys

should_run = True
debug = False
popens = { }

def create_net():
    """
    We have single and multi node version of this network.
    The single node version does not requre any virtual interface.
    Everything runs inside R2DTWO (with internal interfaces).

    The multi-node version is a regular test with multiple mininet host.
    The setup of that network done by this function.

    Subnet addresses crafted from link names e.g.:
    M1 - 1.0.0.0/24
    M2 - 2.0.0.0/24
    Host addresses: R1: .1, R2: .2, E1: .3, E2 .4
    Ordering function implemented on E2

    ┏━━━┓                                             ┏━━━━┓
    ┃ t ┃                                             ┃ l  ┃
    ┗━┯━┛                                             ┗━▲━━┛
      │         M1         ┌────┐  M4                   │
      └─────┐ ┌────────────▶ R2 ●────────────┐          │
            │ │            └─●──┘            │          │
            │ │              │ M5            │          │
          ┌─▼─●┐   M2        │             ┌─▼──┐ End ┌─┴──┐
          │ R1 ●─────────────┼─────────────▶ E2 ●─────▶ O  │
          └───●┘             │             └─▲──┘     └────┘
              │              │               │
              │ M3         ┌─▼──┐  M6        │
              └────────────▶ E1 ●────────────┘
                           └────┘
       ╔══════════════════════════════════════════╗
       ║AutoMIP level = 3                         ║
       ║Naming: o_STREAM_LEVEL_{pre|post}-OBJNAME ║
       ╚══════════════════════════════════════════╝
    """

    net = Mininet(autoStaticArp=True)
    r1, r2, e1, e2, o, l = [net.addHost(n, ip=None) for n in ["r1", "r2", "e1", "e2", "o", "l"]]
    net.addLink(r1, r2, intfName1='r1r2', intfName2='r2r1')
    net.addLink(r1, e2, intfName1='r1e2', intfName2='e2r1')
    net.addLink(r1, e1, intfName1='r1e1', intfName2='e1r1')
    net.addLink(r2, e1, intfName1='r2e1', intfName2='e1r2')
    net.addLink(r2, e2, intfName1='r2e2', intfName2='e2r2')
    net.addLink(e1, e2, intfName1='e1e2', intfName2='e2e1')
    net.addLink(e2, o, intfName1='e1-o', intfName2='o-e2')
    net.addLink(o, l, intfName1='o-l', intfName2='l-o')

    net.build()

    r2.cmd("ip link set dev r2r1 mtu 2000 up")
    r2.cmd("ip link set dev r2e1 mtu 2000 up")
    r2.cmd("ip link set dev r2e2 mtu 2000 up")
    e1.cmd("ip link set dev e1r1 mtu 2000 up")
    e1.cmd("ip link set dev e1r2 mtu 2000 up")
    e1.cmd("ip link set dev e1e2 mtu 2000 up")
    e2.cmd("ip link set dev e2e1 mtu 2000 up")
    e2.cmd("ip link set dev e2r1 mtu 2000 up")
    e2.cmd("ip link set dev e2r2 mtu 2000 up")

    r1.cmd("ip a a 1.0.0.1/24 dev r1r2; ethtool -K r1r2 tx off")
    r1.cmd("ip a a 2.0.0.1/24 dev r1e2; ethtool -K r1e2 tx off")
    r1.cmd("ip a a 3.0.0.1/24 dev r1e1; ethtool -K r1e1 tx off")
    r2.cmd("ip a a 1.0.0.2/24 dev r2r1; ethtool -K r2r1 tx off")
    r2.cmd("ip a a 4.0.0.2/24 dev r2e2; ethtool -K r2e2 tx off")
    r2.cmd("ip a a 5.0.0.2/24 dev r2e1; ethtool -K r2e1 tx off")
    e1.cmd("ip a a 3.0.0.3/24 dev e1r1; ethtool -K e1r1 tx off")
    e1.cmd("ip a a 5.0.0.3/24 dev e1r2; ethtool -K e1r2 tx off")
    e1.cmd("ip a a 6.0.0.3/24 dev e1e2; ethtool -K e1e2 tx off")
    e2.cmd("ip a a 4.0.0.4/24 dev e2r2; ethtool -K e2r2 tx off")
    e2.cmd("ip a a 2.0.0.4/24 dev e2r1; ethtool -K e2r1 tx off")
    e2.cmd("ip a a 6.0.0.4/24 dev e2e1; ethtool -K e2e1 tx off")

    for n in [r1, r2, e1, e2]:
        n.cmd("sysctl -w net.ipv4.ip_forward=1")
    time.sleep(0.2)

    return net

def generate_traffic(count = 0):
    """
    Minimalistic traffic generator: 100pps 100bytes UDP packets.
    Can be received by R2DTWO with a udp-in interface.
    It abuses a mpls.label=0 match.
    """
    global should_run
    sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
    dummypkt = b"\x00" * 100
    sent = 0
    while should_run and sent < count:
        time.sleep(0.01)
        sender.sendto(dummypkt, ("0", 5555))
        sent += 1
    sender.close()

# for multinode
def start_r2dtwos(net, no_signal = True):
    global debug
    global popens

    suffix = "_signaling"
    if no_signal:
        suffix = "_no_signal"
    if debug:
        # exec_bg("tilix -e zellij -s r2masktest")
        # time.sleep(0.1)
        for n in ['r1', 'r2', 'e1', 'e2']:
            node = net.get(n)
            time.sleep(0.2)
            node.popen(f"xterm -T {n} -e env -i ../r2dtwo mask/multinode/{n}{suffix}.ini -vPACKETTRACE:ALL")
            # os.system(f"echo '../r2dtwo mask/multinode/{n}{suffix}.ini -vALL:ALL' >> /tmp/.mnbash_history_{n}")
            # exec_fg(f"zellij -s r2masktest run -- mnbash {n}")
        # exec_fg(f"zellij -s r2masktest action next-swap-layout --")
        CLI(net)
        # exec_fg("killall -9 zellij")
    else: # not debug
        for n in ['r1', 'r2', 'e1', 'e2']:
            node = net.get(n)
            time.sleep(0.2)
            switch_netns(n)
            popens[n] = exec_bg(f"../r2dtwo mask/multinode/{n}{suffix}.ini -vDIAGNOSTIC:ALL", OUT_PIPE)
    time.sleep(0.2)

def command_test_no_signaling():
    print("Test local masking telnet commands...", end=" ")
    success = 0
    try:
        out = exec_bg("../r2dtwo -vALL:NONE,OAM:INFO ./mask/mask_no_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000)
        _ = cli.recv() # recv first msg

        cli.send("mask") # mask command exists?
        msg = cli.recv()
        if "Format: [un]mask PIPENAME" in msg:
            success += 1
        cli.send("unmask") # unmask exists?
        msg = cli.recv()
        if "Format: [un]mask PIPENAME" in msg:
            success += 1
        cli.send("unmask M2") # valid command, but already unmasked
        msg = cli.recv()
        if "pipeline already unmasked" in msg:
            success += 1
        cli.send("mask M2") # valid command, mask but no signaling
        msg = cli.recv()
        if "Pipeline 'M2' masked" in msg and "mep start not found for 'mask' command" in msg:
            success += 1
        cli.close()
        time.sleep(0.1)
        out.terminate()
        # check R2DTWO output
        if "sending 'mask' signal failed" in str(out.communicate()[0]):
            success += 1
    finally:
        cli.close()
        exec_fg("killall r2dtwo")
        if success == 5:
            return 1
        else:
            return 0

def command_test_signaling():
    print("Test masking telnet commands with signaling enabled...", end=" ")
    success = 0
    try:
        out = exec_bg("../r2dtwo -vALL:NONE,OAM:INFO ./mask/mask_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000)
        _ = cli.recv() # recv first msg

        cli.send("unmask M2") # valid command, but already unmasked
        msg = cli.recv()
        if "pipeline already unmasked" in msg:
            success += 1
        cli.send("mask M2") # valid command, mask and signaling
        msg = cli.recv()
        if "Pipeline 'M2' masked" in msg and "request mask" in msg and "o_M2_L3_post-R1" in msg:
            success += 1
        cli.send("unmask M2") # unmask and signaling
        msg = cli.recv()
        if "Pipeline 'M2' unmasked" in msg and "request unmask" in msg and "o_M2_L3_post-R1" :
            success += 1
        out.terminate()
        r2output = str(out.communicate()[0])
        if "type mask mep o_M2_L3_post-R1" in r2output and "type unmask mep o_M2_L3_post-R1" in r2output:
            success += 1
    finally:
        cli.close()
        if success == 4:
            return 1
        else:
            return 0

def loopback_local_mask():
    print("Test local masking without signaling...", end=" ")
    ret = 0
    try:
        out = exec_bg("../r2dtwo -vALL:NONE,DIAGNOSTIC:INFO ./mask/mask_no_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000)
        _ = cli.recv()

        cli.send("mask M2")
        _ = cli.recv()
        time.sleep(0.3)
        generate_traffic(100)
        cli.send("unmask M2")
        _ = cli.recv()
        cli.send("mask M3")
        _ = cli.recv()
        time.sleep(0.3)
        generate_traffic(100)
        out.terminate()
        r2output = str(out.communicate()[0])
        if "E2: DISFUNCTIONING_PATHS" in r2output and "E1: DISFUNCTIONING_PATHS" in r2output:
            ret = 1
    finally:
        cli.close()
        return ret

def loopback_mask_signaling():
    print("Test masking with signaling...", end=" ")
    ret = 1
    try:
        out = exec_bg("../r2dtwo -vALL:NONE,DIAGNOSTIC:INFO ./mask/mask_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000, auto_recv=True)

        cli.send("mask M2")
        time.sleep(0.3)
        generate_traffic(100)
        cli.send("unmask M2")
        cli.send("mask M3")
        time.sleep(0.3)
        generate_traffic(100)
        out.terminate()
        r2output = str(out.communicate()[0])
        if "E2: DISFUNCTIONING_PATHS" in r2output and "E1: DISFUNCTIONING_PATHS" in r2output:
            ret = 0
    finally:
        cli.close()
        return ret

def local_mask():
    print("Test local masking without signaling on multi-node network...", end=" ")
    global popens
    ret = 0
    try:
        net = create_net()
        start_r2dtwos(net)
        switch_netns("r1")
        r1cli = Telnet("0", 8000, auto_recv=True)

        r1cli.send("mask M2")
        time.sleep(0.3)
        generate_traffic(100)
        r1cli.send("unmask M2")
        r1cli.send("mask M3")
        time.sleep(0.3)
        generate_traffic(100)
        time.sleep(1)
        r1cli.close()
        for n in ['r1', 'r2', 'e1', 'e2']:
            popens[n].terminate()
        e1output = str(popens['e1'].communicate()[0])
        e2output = str(popens['e2'].communicate()[0])
        # print(e1output, "\n----------------\n", e2output)
        if "E1: DISFUNCTIONING_PATHS" in e1output and "E2: DISFUNCTIONING_PATHS" in e2output:
            ret = 1
    finally:
        r1cli.close()
        net.stop()
        return ret

def mask_signaling():
    print("Test masking with signaling on multi-node network...", end=" ")
    global popens
    ret = 1
    try:
        net = create_net()
        start_r2dtwos(net, no_signal=False)
        switch_netns("r1")
        r1cli = Telnet("0", 8000, auto_recv=True)

        r1cli.send("mask M2")
        time.sleep(0.3)
        generate_traffic(100)
        r1cli.send("unmask M2")
        r1cli.send("mask M3")
        time.sleep(0.3)
        generate_traffic(100)
        time.sleep(1)
        r1cli.close()
        for n in ['r1', 'r2', 'e1', 'e2']:
            popens[n].terminate()
        e1output = str(popens['e1'].communicate()[0])
        e2output = str(popens['e2'].communicate()[0])
        # print(e1output, "\n----------------\n", e2output)
        if "E1: DISFUNCTIONING_PATHS" in e1output and "E2: DISFUNCTIONING_PATHS" in e2output:
            ret = 0
    finally:
        r1cli.close()
        net.stop()
        return ret


def run_tests():
    tests = [
        command_test_no_signaling,
        command_test_signaling,
        loopback_local_mask,
        loopback_mask_signaling,
        local_mask,
        mask_signaling,
    ]

    sum_result = 0
    for test in tests:
        result = test()
        if result == 1:
            print("✔")
        else:
            print("✘")
        sum_result += result

    print(f'All test completed, {sum_result}/{len(tests)} successfully')
    if sum_result != len(tests):
        exit(1)
    exit(0)


def main():
    global should_run
    global debug
    try:
        exec_fg("killall r2dtwo")
        exec_fg("killall gdb")
        # net = create_net()
        if len(sys.argv) >= 2 and "debug" in sys.argv[1]:
            debug = True
            print("R2DTWO mask debug (uncomment only the desired test)")
        else:
            print("R2DTWO mask test")
        run_tests()
    finally:
        print("Cleanup...")
        should_run = False
        exec_fg("killall -9 r2dtwo")

main()


