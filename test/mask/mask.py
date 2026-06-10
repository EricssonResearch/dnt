#!/usr/bin/python3


from socket import AF_INET, IPPROTO_UDP, SOCK_DGRAM, socket
from mininet.net import Mininet
from mininet.cli import CLI
from select import *
from utils import *
import time
import sys

debug = False
popens = { }

def create_net():
    """
    We have single and multi node version of this network.
    The single node version does not requre any virtual interface.
    Everything runs inside DNT (with internal interfaces).

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

def generate_traffic(node, count):
    """
    Minimalistic traffic generator: 100pps 100bytes UDP packets.
    Can be received by DNT with a udp-in interface.
    It abuses a mpls.label=0 match.
    """
    switch_netns(node)

    sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
    dummypkt = b"\x00" * 100
    sent = 0
    while sent < count:
        time.sleep(0.01)
        sender.sendto(dummypkt, ("0", 5555))
        sent += 1
    sender.close()

# for multinode
def start_dnts(net, no_signal = True):
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
            node.popen(f"xterm -T {n} -e env -i ../dnt mask/multinode/{n}{suffix}.ini -vPACKETTRACE:ALL")
            # os.system(f"echo '../dnt mask/multinode/{n}{suffix}.ini -vALL:ALL' >> /tmp/.mnbash_history_{n}")
            # exec_fg(f"zellij -s r2masktest run -- mnbash {n}")
        # exec_fg(f"zellij -s r2masktest action next-swap-layout --")
        CLI(net)
        # exec_fg("killall -9 zellij")
    else: # not debug
        for n in ['r1', 'r2', 'e1', 'e2']:
            node = net.get(n)
            time.sleep(0.2)
            switch_netns(n)
            popens[n] = exec_bg(f"../dnt mask/multinode/{n}{suffix}.ini -vDIAGNOSTIC:ALL", OUT_PIPE)
    time.sleep(0.2)

def command_test_no_signaling():
    print("Masking telnet commands without signalling...", end=" ")
    commands = [
            ("mask",
"""
mask state for SequenceRecovery 'E1'
  latent error paths 2 / 2
    o_M5_L3_pre-E1 is not masked
    o_M3_L3_pre-E1 is not masked
mask state for Replicate 'R1'
  pipeline 'M1' is not masked
  pipeline 'M2' is not masked
  pipeline 'M3' is not masked
mask state for SequenceRecovery 'E2'
  latent error paths 3 / 3
    o_M2_L3_pre-E2 is not masked
    o_M4_L3_pre-E2 is not masked
    o_M6_L3_pre-E2 is not masked
mask state for Replicate 'R2'
  pipeline 'M4' is not masked
  pipeline 'M5' is not masked
"""),
            ("unmask",
"""
mask state for SequenceRecovery 'E1'
  latent error paths 2 / 2
    o_M5_L3_pre-E1 is not masked
    o_M3_L3_pre-E1 is not masked
mask state for Replicate 'R1'
  pipeline 'M1' is not masked
  pipeline 'M2' is not masked
  pipeline 'M3' is not masked
mask state for SequenceRecovery 'E2'
  latent error paths 3 / 3
    o_M2_L3_pre-E2 is not masked
    o_M4_L3_pre-E2 is not masked
    o_M6_L3_pre-E2 is not masked
mask state for Replicate 'R2'
  pipeline 'M4' is not masked
  pipeline 'M5' is not masked
"""),
            ("mask M2", "Pipeline 'M2' in Replicate R1 now masked"),
            ("mask M2", "Pipeline 'M2' in Replicate R1 already masked"),
            ("unmask M2", "Pipeline 'M2' in Replicate R1 now unmasked"),
            ("unmask M2", "Pipeline 'M2' in Replicate R1 already unmasked"),
            ("mask nopipe", "No pipelines are named 'nopipe'"),
            ("unmask nopipe", "No pipelines are named 'nopipe'"),
            ]

    success = 0
    try:
        out = exec_bg("../dnt -vALL:NONE,OAM:INFO ./mask/mask_no_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000)
        _ = cli.recv() # 'OAM ready'

        for msg, expected in commands:
            cli.send(msg)
            reply = cli.recv()

            if expected.strip() == reply.strip():
                success += 1
            else:
                print(f"Command '{msg}'\nExpected reply\n{expected}\nActual reply\n{reply}\n")

        cli.close()
        time.sleep(0.1)
        out.terminate()
    finally:
        cli.close()
        exec_fg("killall dnt")
        if success == len(commands):
            return 1
        else:
            return 0

def command_test_signaling():
    print("Masking telnet commands with signaling enabled...", end=" ")
    commands = [
            ("unmask M2", "Pipeline 'M2' in Replicate R1 already unmasked"),
            ("mask M2", """
Pipeline 'M2' in Replicate R1 now masked
Initiated mask signalling from MIP 'o_M2_L3_post-R1'
"""),
            ("mask",
"""
mask state for SequenceRecovery 'E1'
  latent error paths 2 / 2
    o_M5_L3_pre-E1 is not masked
    o_M3_L3_pre-E1 is not masked
mask state for Replicate 'R1'
  pipeline 'M1' is not masked
  pipeline 'M2' is masked, o_M2_L3_post-R1 sending mask signal
  pipeline 'M3' is not masked
mask state for SequenceRecovery 'E2'
  latent error paths 2 / 3
    o_M2_L3_pre-E2 is masked
    o_M4_L3_pre-E2 is not masked
    o_M6_L3_pre-E2 is not masked
mask state for Replicate 'R2'
  pipeline 'M4' is not masked
  pipeline 'M5' is not masked
"""),
            ("unmask M2", """
Pipeline 'M2' in Replicate R1 now unmasked
Stopped mask signalling from MIP 'o_M2_L3_post-R1'
"""),

            # masking M3 and M5 also masks M6
            ("mask M3", """
Pipeline 'M3' in Replicate R1 now masked
Initiated mask signalling from MIP 'o_M3_L3_post-R1'
"""),
            ("mask M5", """
Pipeline 'M5' in Replicate R2 now masked
Initiated mask signalling from MIP 'o_M5_L3_post-R2'
"""),
            ("mask", """
mask state for SequenceRecovery 'E1'
  latent error paths 0 / 2
    o_M5_L3_pre-E1 is masked
    o_M3_L3_pre-E1 is masked
mask state for Replicate 'R1'
  pipeline 'M1' is not masked
  pipeline 'M2' is not masked
  pipeline 'M3' is masked, o_M3_L3_post-R1 sending mask signal
mask state for SequenceRecovery 'E2'
  latent error paths 2 / 3
    o_M2_L3_pre-E2 is not masked
    o_M4_L3_pre-E2 is not masked
    o_M6_L3_pre-E2 is masked
mask state for Replicate 'R2'
  pipeline 'M4' is not masked
  pipeline 'M5' is masked, o_M5_L3_post-R2 sending mask signal
"""),
            # OAM ping should go through on masked branches
            ("ping o_M1_L3_post-R1 o_M5_L3_pre-E1 3", """
ping o_M1_L3_post-R1 -> o_M5_L3_pre-E1 stream M1 session 1 level 3 count 1 interval 1000 [reply to ip 127.0.0.1 port 6666]
  ping reply from o_M5_L3_pre-E1 [target o_M5_L3_pre-E1] stream M1 session 1 level 3 seq 0
"""),
            ("ping o_M1_L3_post-R1 o_M6_L3_pre-E2 3", """
ping o_M1_L3_post-R1 -> o_M6_L3_pre-E2 stream M1 session 2 level 3 count 1 interval 1000 [reply to ip 127.0.0.1 port 6666]
  ping reply from o_M6_L3_pre-E2 [target o_M6_L3_pre-E2] stream M1 session 2 level 3 seq 0
"""),
            ("unmask M5", """
Pipeline 'M5' in Replicate R2 now unmasked
Stopped mask signalling from MIP 'o_M5_L3_post-R2'
"""),
            ("mask", """
mask state for SequenceRecovery 'E1'
  latent error paths 1 / 2
    o_M5_L3_pre-E1 is not masked
    o_M3_L3_pre-E1 is masked
mask state for Replicate 'R1'
  pipeline 'M1' is not masked
  pipeline 'M2' is not masked
  pipeline 'M3' is masked, o_M3_L3_post-R1 sending mask signal
mask state for SequenceRecovery 'E2'
  latent error paths 3 / 3
    o_M2_L3_pre-E2 is not masked
    o_M4_L3_pre-E2 is not masked
    o_M6_L3_pre-E2 is not masked
mask state for Replicate 'R2'
  pipeline 'M4' is not masked
  pipeline 'M5' is not masked
"""),
            ]

    success = 0
    try:
        out = exec_bg("../dnt -vALL:NONE,OAM:INFO ./mask/mask_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000)
        _ = cli.recv() # 'OAM ready'

        for msg, expected in commands:
            cli.send(msg)
            reply = cli.recv()

            if expected.strip() == reply.strip():
                success += 1
            else:
                print(f"Command '{msg}'\nExpected reply\n{expected}\nActual reply\n{reply}\n")

        out.terminate()
        time.sleep(0.1)
        r2output = str(out.communicate()[0])

        if "type mask mep o_M2_L3_post-R1" in r2output and "type unmask mep o_M2_L3_post-R1" in r2output:
            success += 1
    finally:
        cli.close()
        exec_fg("killall dnt")
        if success == len(commands):
            return 1
        else:
            return 0

def loopback_local_mask():
    print("Local masking without signaling...", end=" ")
    ret = 0
    try:
        out = exec_bg("../dnt -vALL:NONE,DIAGNOSTIC:INFO ./mask/mask_no_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000, auto_recv=True)

        cli.send("mask M2")
        time.sleep(0.1)
        generate_traffic(None, 100)
        time.sleep(0.1)
        cli.send("unmask M2")
        cli.send("mask M3")
        time.sleep(0.1)
        generate_traffic(None, 100)
        out.terminate()
        r2output = str(out.communicate()[0])
        #print(r2output)
        if "E2: DISFUNCTIONING_PATHS" in r2output and "E1: DISFUNCTIONING_PATHS" in r2output:
            ret = 1
    except Exception as ex:
        print(type(ex))
        cli.close()
        ret = 0
    finally:
        cli.close()
        return ret

def loopback_mask_signaling():
    print("Masking with signaling...", end=" ")
    ret = 1
    try:
        out = exec_bg("../dnt -vALL:NONE,DIAGNOSTIC:INFO ./mask/mask_signaling.ini", OUT_PIPE)
        time.sleep(0.2)
        cli = Telnet("0", 8000, auto_recv=True)

        cli.send("mask M2\n")
        time.sleep(0.1)
        generate_traffic(None, 100)
        time.sleep(0.1)
        cli.send("unmask M2")
        cli.send("mask M3")
        time.sleep(0.1)
        generate_traffic(None, 100)
        out.terminate()
        r2output = str(out.communicate()[0])
        #print(r2output)
        if "E1: DISFUNCTIONING_PATHS" in r2output or "E2: DISFUNCTIONING_PATHS" in r2output:
            ret = 0
    except Exception as ex:
        print(type(ex))
        cli.close()
        ret = 0
    finally:
        cli.close()
        return ret

def local_mask():
    print("Local masking without signaling on multi-node network...", end=" ")
    global popens
    ret = 0
    try:
        net = create_net()
        start_dnts(net)
        switch_netns("r1")
        r1cli = Telnet("0", 8000, auto_recv=True)

        r1cli.send("mask M2")
        time.sleep(0.1)
        generate_traffic("r1", 100)
        time.sleep(0.1)
        r1cli.send("unmask M2")
        r1cli.send("mask M3")
        time.sleep(0.1)
        generate_traffic("r1", 100)
        time.sleep(0.1)
        r1cli.close()
        for n in ['r1', 'r2', 'e1', 'e2']:
            popens[n].terminate()
        e1output = str(popens['e1'].communicate()[0])
        e2output = str(popens['e2'].communicate()[0])
        # print(e1output, "\n----------------\n", e2output)
        if "E1: DISFUNCTIONING_PATHS" in e1output or "E2: DISFUNCTIONING_PATHS" in e2output:
            ret = 1
    except Exception as ex:
        print(type(ex))
        r1cli.close()
        ret = 0
    finally:
        r1cli.close()
        net.stop()
        return ret

def mask_signaling():
    print("Masking with signaling on multi-node network...", end=" ")
    global popens
    ret = 1
    try:
        net = create_net()
        start_dnts(net, no_signal=False)
        switch_netns("r1")
        r1cli = Telnet("0", 8000, auto_recv=True)

        r1cli.send("mask M2")
        time.sleep(0.1)
        generate_traffic("r1", 100)
        time.sleep(0.1)
        r1cli.send("unmask M2")
        r1cli.send("mask M3")
        time.sleep(0.1)
        generate_traffic("r1", 100)
        time.sleep(0.1)
        r1cli.close()
        for n in ['r1', 'r2', 'e1', 'e2']:
            popens[n].terminate()
        e1output = str(popens['e1'].communicate()[0])
        e2output = str(popens['e2'].communicate()[0])
        # print(e1output, "\n----------------\n", e2output)
        if "E1: DISFUNCTIONING_PATHS" in e1output and "E2: DISFUNCTIONING_PATHS" in e2output:
            ret = 0
    except Exception as ex:
        print(type(ex))
        r1cli.close()
        ret = 0
    finally:
        r1cli.close()
        net.stop()
        return ret


def run_tests():
    tests = [
        command_test_no_signaling, #ok
        command_test_signaling, #ok
        loopback_local_mask, #ok
        loopback_mask_signaling,
        # must enable one of these for debugging cli
        local_mask, #ok
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
        return False
    return True


def main():
    global debug
    all_ok = False
    try:
        exec_fg("killall dnt")
        exec_fg("killall gdb")
        # net = create_net()
        if len(sys.argv) >= 2 and "debug" in sys.argv[1]:
            debug = True
            print("DNT mask debug (uncomment only the desired test)")
        else:
            print("DNT mask test")
        all_ok = run_tests()
    finally:
        print("Cleanup...")
        exec_fg("killall -9 dnt")
        if all_ok:
            exit(0)
        exit(1)

main()


