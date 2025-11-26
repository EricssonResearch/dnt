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

import sys
sys.path.append('../json_receiver/')
from notification_receiver import NotificationReceiver  # Assuming you save the class in this file

NUM_PACKETS_S2 = 5
NUM_PACKETS_S3 = 5
pkts_delay = [
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=1000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=2000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=1000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=2000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=1000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=2000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=1000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=2000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=1000)/IP()/UDP(),
    Ether(dst="0a:bb:cc:aa:bb:cc")/Dot1Q(vlan=2000)/IP()/UDP(),
]

notif_messages=[]       # hold the messages received

class UDPReceiver(asyncio.DatagramProtocol):
    def __init__(self, version, receiver):
        self.version = version  # Track whether it's IPv4 or IPv6
        self.receiver = receiver

    def datagram_received(self, data, addr):
        global notif_messages

        if self.version == "IPv6":
            host, port, *_ = addr  # Unpack only first two values (IPv6 has extra fields)
        else:
            host, port = addr  # Standard IPv4 format

        json_received = self.receiver.process_notification(host, port, data)
        if json_received is not None:
            notif_messages.append(json_received)


    def connection_lost(self, exc):
        print(f"[{self.version}] receiver closed.")

    def error_received(self, exc):
        print("Exception thrown: %s" % exc)

async def start_udp_listener(host, port, family, version, transports, receiver):
    loop = asyncio.get_running_loop()
    listen = loop.create_datagram_endpoint(
        lambda: UDPReceiver(version, receiver),
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
    return exec_bg("../r2dtwo notification/notification.ini")
#    return exec_bg("../r2dtwo -of notification/notification.ini -v PACKETTRACE:PACKET")

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
        "ip link set dev r2tx up"
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

    cli.send("notif_pull")
    msg = cli.recv()
    if "Notification pull is disabled" not in msg:
        print("Error: ", msg)
        ret = False

    cli.send("notif_pull enable")
    msg = cli.recv()
    if "Notification pull is now enabled" not in msg:
        print("Error: ", msg)
        ret = False

    cli.send("mask to_nni0") # send mask command
    msg = cli.recv()
    if "Pipeline 'to_nni0' in Replicate rep now masked" not in msg:
        print("Error: ", msg)
        ret = False

    cli.send("sysmon add tc r2rx") # send add notification command
    msg = cli.recv()
    if "Success" not in msg:
        print("Error: ", msg)
        ret = False

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

def test_notifications():
    failed = 0

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
    for notif_msg in notif_messages:
        print(f"Received last -> {notif_msg}")      # comment to have a clean output
        msg = notif_msg.get("notif_msg")
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
        if msg.get("rep"):
            rep_msg = msg
        if msg.get("srcvy1"):
            rec_msg = msg

    print("Test notification message replication...", end=" ")
    seq = notif_msg.get("notif_seq")    # seq from last message
    if len(notif_messages) == seq+1:
        print("✔")
    else:
        print(f"✘ - received {len(notif_messages)}, expected {seq+1}")
        failed = failed+1

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
                  "send_packets", NUM_PACKETS_S2+NUM_PACKETS_S3, "Received {} sent packets - should be {}"]
    failed = failed + validate_json(if_msg, checks)

    print("\nTest if1 parser report...", end=" ")
    checks = [ "if1 parser", "No if1 parser statistic received.",
                  "s2 packets", NUM_PACKETS_S2, "Received {} s2 packets - should be {}",
                  "s3 packets", NUM_PACKETS_S3, "Received {} s3 packets - should be {}" ]
    failed = failed + validate_json(parser_msg, checks)

    print("\nTest replication report...", end=" ")
    checks = [ "rep", "No replication statistic received.",
                  "packets_passed", NUM_PACKETS_S2, "Received {} passed packets - should be {}"]
    f=validate_json(rep_msg, checks)
    if f == 0:
        rep_js = rep_msg.get("rep")
        checks = [ "pipelines", "No pipelines in replication statistic.",
                     "mask_state",  "masked", "Mask state is {} - should be {}"]
        failed = failed + validate_json(rep_js, checks)
    else:
        failed = failed + f

    print("\nTest sequence recovery report...", end=" ")
    checks = [ "srcvy1", "No sequence recovery statistic received.",
                  "passed_packets", NUM_PACKETS_S2, "Received {} passed packets - should be {}"]
    failed = failed + validate_json(rec_msg, checks)

    print()

    return failed

async def main():
    transports = []  # Store UDP transports

    print("R2DTWO notifications tests - TSN")
    ret = 0

    receiver = NotificationReceiver(seq_history_size=200)
    # Start both IPv4 and IPv6 listeners
    task_ipv4 = asyncio.create_task(start_udp_listener("127.0.0.1", 9000, AF_INET, "IPv4", transports, receiver))
    task_ipv6 = asyncio.create_task(start_udp_listener("::1", 9600, AF_INET6, "IPv6", transports, receiver))
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
    result = test_notifications()
    if result == -1:
        print("Error running tests.")

    exec_fg("killall r2dtwo")
    await cleanup_udp(transports)
    cleanup_ifaces()

    if result != 0:
        exit(1)
    exit(0)

asyncio.run(main())  # Start the event loop
