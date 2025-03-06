#!/usr/bin/python3

from mininet.cli import CLI
from scapy.layers.l2 import Ether, Dot1Q
from scapy.layers.inet import IP, UDP
from scapy.all import sendp
from utils import *
import json
import socket
import time
import asyncio

SEQ_HISTORY_SIZE = 10
dups = 0
index = 0
last_seqnums=[]
notif_messages=[]       # hold the messages received

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
    #print("\nShutting down UDP receivers...")
    for transport in transports:
        transport.close()  # Close UDP sockets
    await asyncio.sleep(1)  # Allow time for cleanup messages

def start_r2dtwo():
#    return exec_bg("../r2dtwo notification/notification.ini")
    return exec_bg("../r2dtwo -of notification/notification.ini -v PACKETTRACE:PACKET")

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
    cli.close()
    return ret

async def test_delay():
    global index, dup, notifications
    failed = 0

    config_ifaces()
    start_r2dtwo()
    await asyncio.sleep(0.3)
    if not send_cli_commands():
        print("Cli commands failed.")
        return -1

    if exec_fg("ip a a 10.0.0.1/24 dev r2rx", silent=False).returncode != 0:
        print("Could not set ip address.")
        return -1

    await asyncio.sleep(1)  # wait
    sendp(pkts_delay, verbose=0, iface="to_r2")
    await asyncio.sleep(3)

    print("Test notification message replication...", end=" ")
    if index == dups:
        print("✔")
    else:
        print("✘")
        failed = failed+1

    telnet_push = False
    mask_push = False
    newip_push = False
    for msg in notif_messages:
        #print(f"Received last -> {msg}")
        if msg.get("telnet"):
            telnet_push = True
        if msg.get("mask"):
            mask_push = True
        if msg.get("new src"):
            newip_push = True
        if msg.get("if1"):
            last_msg = msg

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
    dly_js = last_msg.get("delay").get("s3")
    if dly_js is None:
        print("✘  - No delay statistic received.")
        failed = failed + 1
    else:
        dly_exceeded = dly_js.get("delay_exceeded_packets")
        dly = dly_js.get("delayed_packets")
        if dly == 10 and dly_exceeded == 5:
            print("✔")
        else:
            print(f"✘ - Received {dly} delayed and {dly_exceeded} delay exceeded packets - should be 10 and 5")
            failed = failed+1

    print("Test tc notification report...", end=" ")
    tc_js = last_msg.get("zxdelay").get("s3")
    if tc_js is None:
        print("✘  - No TC statistic received.")
        failed = failed + 1
    else:
        print("✔")

    print("Test if2 report...", end=" ")
    if_js = last_msg.get("if2")
    if if_js is None:
        print("✘  - No if2 statistic received.")
        failed = failed + 1
    else:
        sent = if_js.get("send_packets")
        if sent == NUM_PACKETS_S2*3:
            print("✔")
        else:
            print(f"✘ - Received {sent} sent packets - should be {NUM_PACKETS_S2*3}")
            failed = failed+1

    print("Test replication report...", end=" ")
    rep_js = last_msg.get("rep")
    if rep_js is None:
        print("✘  - No replication statistic received.")
        failed = failed + 1
    else:
        rep_pass = rep_js.get("packets_passed")
        pipe_js = rep_js.get("pipelines")
        first_mask_state = pipe_js[0].get("mask_state")
        if rep_pass == NUM_PACKETS_S2 and "masked" in first_mask_state:
            print("✔")
        else:
            print(f"✘ - Received {rep_pass} passed packets - should be {NUM_PACKETS_S2}, mask state {first_mask_state} should be 'masked'")
            failed = failed+1

    print("Test sequence recovery report...", end=" ")
    rec_js = last_msg.get("srcvy1")
    if rec_js is None:
        print("✘  - No sequence recovery statistic received.")
        failed = failed + 1
    else:
        rec_pass = rec_js.get("passed_packets")
        if rec_pass == NUM_PACKETS_S2:
            print("✔")
        else:
            print(f"✘ - Received {rec_pass} passed packets - should be {NUM_PACKETS_S2}")
            failed = failed+1

    return failed

async def main():
    transports = []  # Store UDP transports

    print("R2DTWO notifications tests")
    ret = 0
    # Start both IPv4 and IPv6 listeners
    task_ipv4 = asyncio.create_task(start_udp_listener("127.0.0.1", 9000, AF_INET, "IPv4", transports))
    task_ipv6 = asyncio.create_task(start_udp_listener("::1", 9600, AF_INET6, "IPv6", transports))
    await asyncio.gather(task_ipv4, task_ipv6)  # Keep tasks running

    result = await test_delay()
    if result == -1:
        print("Error running tests.")

    exec_fg("killall r2dtwo")
    await cleanup_udp(transports)
    cleanup_ifaces()

    if result != 0:
        exit(1)
    exit(0)

asyncio.run(main())  # Start the event loop
