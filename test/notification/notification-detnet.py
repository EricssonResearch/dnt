#!/usr/bin/python3

from mininet.cli import CLI
from scapy.layers.l2 import Ether, Dot1Q
from scapy.layers.inet import IP, UDP
from scapy.all import sendp
from utils import *
import json
import socket
import time, os
import asyncio
import select
import threading

NUM_PACKETS_S2 = 5
NUM_PACKETS_S3 = 5
pkts_delay = [
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="8.8.8.8")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="8.8.8.8")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="8.8.8.8")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="8.8.8.8")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="7.7.7.7")/UDP(),
    Ether(dst="ab:bb:cc:aa:bb:cd")/IP(src="9.9.9.9", dst="8.8.8.8")/UDP()
]

SEQ_HISTORY_SIZE = 10
dups = 0
index = 0
last_seqnums=[]
notif_messages=[]       # hold the messages received

class UDPReceiver(asyncio.DatagramProtocol):
    def __init__(self, version):
        self.version = version  # Track whether it's IPv4 or IPv6

    def datagram_received(self, data, addr):
        global dups, index, last_seqnums

        if self.version == "IPv6":
            host, port, *_ = addr  # Unpack only first two values (IPv6 has extra fields)
        else:
            host, port = addr  # Standard IPv4 format

        try:
            jsonReceived = json.loads(data.decode())
            #print(jsonReceived)
            seq_num = jsonReceived.get("notif_seq")
            if seq_num != None:
                if seq_num in last_seqnums:
                    dups = dups + 1
                else:
                    last_seqnums.append(seq_num)
                    index = ( index + 1 ) % SEQ_HISTORY_SIZE
                    notif_messages.append(jsonReceived)
        except json.JSONDecodeError:
            print(f"[{self.version}] Invalid JSON from {host}:{port}: {data}")

    def connection_lost(self, exc):
        print(f"[{self.version}] receiver closed.")

    def error_received(self, exc):
        print("Exception thrown: %s" % exc)

async def start_udp_listener(host, port, family, version, transports):
    loop = asyncio.get_running_loop()
    listen = loop.create_datagram_endpoint(
        lambda: UDPReceiver(version),
        local_addr=(host, port),
        family=family
    )
    transport, _ = await listen
    transports.append(transport)  # Store transport for cleanup

# Cleanup function
async def cleanup_udp(transports):
    for transport in transports:
        transport.close()  # Close UDP sockets
    await asyncio.sleep(1)  # Allow time for cleanup messages

def start_r2dtwo():
    return exec_bg("../r2dtwo notification/notification-detnet.ini")
#    return exec_bg("../r2dtwo -of notification/notification-detnet.ini -v PACKETTRACE:PACKET")

def cleanup_ifaces():
    exec_fg("ip link del to_r2 type veth peer name r2rx")
    exec_fg("ip link del from_r2 type veth peer name r2tx")

def config_ifaces():
    ret = 0
    exec_fg("ip link del to_r2 type veth peer name r2rx")
    exec_fg("ip link del from_r2 type veth peer name r2tx")
    cmds = [
        "ip link add to_r2 type veth peer name r2rx",
        "ip link add from_r2 type veth peer name r2tx",
        "sysctl -w net.ipv6.conf.r2rx.disable_ipv6=1",
        "sysctl -w net.ipv6.conf.to_r2.disable_ipv6=1",
        "sysctl -w net.ipv6.conf.r2tx.disable_ipv6=1",
        "sysctl -w net.ipv6.conf.from_r2.disable_ipv6=1",
        "ip link set dev to_r2 up",
        "ip link set dev r2rx up",
        "ip link set dev from_r2 up",
        "ip link set dev r2tx up",
        "ip a a 10.0.2.1/24 dev r2tx"
    ]
    for cmd in cmds:
        ret += exec_fg(cmd).returncode
    if ret > 0:
        print("Error(s) during interface config. Running without sudo?")
        exit(1)

def get_rxpktsnum(iface = "from_r2"):
    out = exec_fg(f"ip -j -p stats show group link dev {iface}")
    print(out.stdout)
    return int(json.loads(out.stdout)[0]["stats64"]["rx"]["packets"])

def send_cli_commands():
    ret = True
    cli = Telnet("0", 8000)
    _ = cli.recv() # recv first msg

    cli.send("mask to_nni0") # send mask command
    msg = cli.recv()
    if "Pipeline 'to_nni0' masked" not in msg:
        print("Error: ", msg)
        ret = False

    cli.send("notification_source add tc r2rx") # send add notification command
    msg = cli.recv()
    if "Success" not in msg:
        print("Error: ", msg)
        ret = False

    cli.send("notif_trigger o_s2_L5_pre-prf o_common_actions_L5_post-srcvy1 5 -n 3 -i 0.05 ") # send notif_trigger command
    msg = cli.recv()
    if "OAM request trigger session" not in msg:
        print("Error: ", msg)
        ret = False

    # wait for the previously requested packets
    time.sleep(1)

    cli.send("exit") # send add notification command
    msg = cli.recv()
    time.sleep(0.1)

    cli.close()
    return ret

def validate_json(json_msg, checks):
    failed = 0

    js = None
    if json_msg is not None:
        js = json_msg.get(checks[0])

    if js is None:
        print(f"✘ - {checks[1]}")
        return failed + 1

    # If the value is a list, get the first element
    if isinstance(js, list):
        js = js[0]

    for i in range(2, len(checks), 3):
        key = checks[i]
        expected_value = checks[i + 1]
        error_message = checks[i + 2]

        pkt = js.get(key)
        if pkt == expected_value:
            print(f"{key} ✔", end=" ")
        else:
            print(f"✘ - {error_message.format(pkt, expected_value)}")
            failed += 1

    return failed

async def start_meas():
    print("Sending Telnet commands")
    if not send_cli_commands():
        print("Cli commands failed.")
        return -1

    if exec_fg("ip a a 10.0.0.1/24 dev r2rx", silent=False).returncode != 0:
        print("Could not set ip address.")
        return -1

    time.sleep(1)

    print("Sending traffic")
    sendp(pkts_delay, verbose=0, iface="to_r2")
    return 0

def test_delay():
    global dups, notifications
    failed = 0

    print("Test notification message replication...", end=" ")
    if len(notif_messages) == dups:
        print("✔")
    else:
        print(f"✘ - received {len(notif_messages)} and {dups} duplicate notifications")
        failed = failed+1

    # collect the latest messages for each notification
    telnet_push = False
    mask_push = False
    newip_push = False
    if_msg = None
    parser_msg = None
    tc_msg = None
    delay_msg = None
    rep_msg = None
    rec_msg = None
    post_seqrec_msg = None
    pre_seqrec_nni0_msg = None
    pre_seqrec_nni1_msg = None
    pre_prf_msg = None
    post_prf_nni0_msg = None
    post_prf_nni1_msg = None
    trig_start = None
    trig = None
    for msg in notif_messages:
        print(f"Received last -> {msg}")
        if msg.get("telnet"):
            telnet_push = True
        if msg.get("mask"):
            mask_push = True
        if msg.get("new src"):
            newip_push = True
        if msg.get("if2"):
            if_msg = msg
        if msg.get("if1 parser"):
            parser_msg = msg
        if msg.get("delay"):
            delay_msg = msg
        if msg.get("tc_r2rx"):
            tc_msg = msg
        if msg.get("prf"):
            rep_msg = msg
        if msg.get("srcvy1"):
            rec_msg = msg
        if msg.get("o_common_actions_L5_post-srcvy1"):
            post_seqrec_msg = msg
        if msg.get("o_to_nni0_L5_pre-srcvy1"):
            pre_seqrec_nni0_msg = msg
        if msg.get("o_to_nni1_L5_pre-srcvy1"):
            pre_seqrec_nni1_msg = msg
        if msg.get("o_s2_L5_pre-prf"):
            pre_prf_msg = msg
        if msg.get("o_to_nni0_L5_post-prf"):
            post_prf_nni0_msg = msg
        if msg.get("o_to_nni1_L5_post-prf"):
            post_prf_nni1_msg = msg
        if msg.get("triggered_source"):
            trig_start = msg
        if msg.get("triggered_receiver"):
            trig = msg


    print("Test telnet login push notification...", end=" ")
    if telnet_push:
        print("✔")
    else:
        print("✘")
        failed = failed + 1

    print("Test mask push notification...", end=" ")
    if mask_push:
        print("✔")
    else:
        print("✘")
        failed = failed + 1

    print("Test IP change push notification...", end=" ")
    if newip_push:
        print("✔")
    else:
        print("✘")
        failed = failed + 1

    print("Test notif_trigger at mep start...", end=" ")
    if trig_start:
        print("✔")
    else:
        print("✘")
        failed = failed + 1

    print("Test notif_trigger at mep stop...", end=" ")
    if trig:
        print("✔")
    else:
        print("✘")
        failed = failed + 1

    print("Test delay notification report...", end=" ")
    checks = [ "s3", "No stream s3 delay statistic received.",
                  "delayed_packets", NUM_PACKETS_S2*2, "Received {} delayed packets - should be {}",
                  "delay_exceeded_packets", NUM_PACKETS_S2, "Received {} delay exceeded packets - should be {}" ]
    dly_js = None
    if delay_msg is not None:
        dly_js = delay_msg.get("delay")
    if dly_js is None:
        print("✘  - No delay statistic received.")
        failed = failed + 1
    else:
        failed = failed + validate_json(dly_js, checks)

    print("\nTest tc notification report...", end=" ")
    checks = [ "tc_r2rx", "No TC statistic received.",
                  "root", True, "Root is {} - should be {}"]
    failed = failed + validate_json(if_msg, checks)

    print("\nTest if2 report...", end=" ")
    checks = [ "if2", "No if2 statistic received.",
                  "send_packets", NUM_PACKETS_S3, "Received {} sent packets - should be {}"]
    failed = failed + validate_json(if_msg, checks)

    print("\nTest if1 parser report...", end=" ")
    checks = [ "if1 parser", "No if1 parser statistic received.",
                  "s2 packets", NUM_PACKETS_S2, "Received {} s2 packets - should be {}",
                  "s3 packets", NUM_PACKETS_S3, "Received {} s3 packets - should be {}" ]
    failed = failed + validate_json(parser_msg, checks)

    print("\nTest replication report...", end=" ")
    checks = [ "prf", "No replication statistic received.",
                  "packets_passed", NUM_PACKETS_S2+3, "Received {} passed packets - should be {}"]
    f=validate_json(rep_msg, checks)
    if f == 0:
        rep_js = rep_msg.get("prf")
        checks = [ "pipelines", "No pipelines in replication statistic.",
                     "mask_state",  "masked", "Mask state is {} - should be {}"]
        failed = failed + validate_json(rep_js, checks)
    else:
        failed = failed + f

    print("\nTest sequence recovery report...", end=" ")
    checks = [ "srcvy1", "No sequence recovery statistic received.",
                  "passed_packets", NUM_PACKETS_S2, "Received {} passed packets - should be {}"]
    failed = failed + validate_json(rec_msg, checks)

    print("\nTest post-SeqRec MIP report...", end=" ")
    checks = [ "o_common_actions_L5_post-srcvy1", "No post-SeqRec MIP statistic received.",
                  "mask_signal_state", "unmasked", "Mask state {} - should be {}",
                  "packets_passed", NUM_PACKETS_S3, "Passed {} packets - should be {}" ]
    failed = failed + validate_json(post_seqrec_msg, checks)

    print("\nTest pre-SeqRec nni0 MIP report...", end=" ")
    checks = [ "o_to_nni0_L5_pre-srcvy1", "No post-SeqRec MIP statistic received.",
                  "mask_signal_state", "unmasked", "Mask state {} - should be {}",
                  "packets_passed", 0, "Passed {} packets - should be {}" ]
    failed = failed + validate_json(pre_seqrec_nni0_msg, checks)

    print("\nTest pre-SeqRec nni1 MIP report...", end=" ")
    checks = [ "o_to_nni1_L5_pre-srcvy1", "No post-SeqRec MIP statistic received.",
                  "mask_signal_state", "unmasked", "Mask state {} - should be {}",
                  "packets_passed", NUM_PACKETS_S3, "Passed {} packets - should be {}" ]
    failed = failed + validate_json(pre_seqrec_nni1_msg, checks)

    print("\nTest pre-Replication MIP report...", end=" ")
    checks = [ "o_s2_L5_pre-prf", "No pre-Replication MIP statistic received.",
                  "mask_signal_state", "unmasked", "Mask state {} - should be {}",
                  "packets_passed", NUM_PACKETS_S3, "Passed {} packets - should be {}" ]
    failed = failed + validate_json(pre_prf_msg, checks)

    print("\nTest post-Replication nni0 MIP report...", end=" ")
    checks = [ "o_to_nni0_L5_post-prf", "No post-Replication nni0 statistic received.",
                  "mask_signal_state", "unmasked", "Mask state {} - should be {}",
                  "packets_passed", 0, "Passed {} packets - should be {}" ]
    failed = failed + validate_json(post_prf_nni0_msg, checks)

    print("\nTest post-Replication nni1 MIP report...", end=" ")
    checks = [ "o_to_nni1_L5_post-prf", "No post-Replication nni1 statistic received.",
                  "mask_signal_state", "unmasked", "Mask state {} - should be {}",
                  "packets_passed", NUM_PACKETS_S3, "Passed {} packets - should be {}" ]
    failed = failed + validate_json(post_prf_nni1_msg, checks)

    print()

    return failed

async def main():
    global stop_event;
    transports = []  # Store UDP transports

    print("R2DTWO notifications tests - DetNet")
    ret = 0

    # Start both IPv4 and IPv6 listeners
    task_ipv4 = asyncio.create_task(start_udp_listener("127.0.0.1", 9000, AF_INET, "IPv4", transports))
    task_ipv6 = asyncio.create_task(start_udp_listener("::1", 9600, AF_INET6, "IPv6", transports))
    await asyncio.gather(task_ipv4, task_ipv6)  # Keep tasks running

    result = 0
    config_ifaces()

    start_r2dtwo()
    # run with sudo ../r2dtwo -of notification/notification-detnet.ini -v ALL:ALL
    #input("Press Enter to continue...")

    await asyncio.sleep(1)
    await start_meas()

    print("Waiting for results")
    await asyncio.sleep(6)

    print("Checking results:")
    result = test_delay()
    if result == -1:
        print("Error running tests.")

    exec_fg("killall r2dtwo")
    await cleanup_udp(transports)
    cleanup_ifaces()

    if result != 0:
        exit(1)
    exit(0)

asyncio.run(main())  # Start the event loop
