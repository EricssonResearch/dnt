#!/usr/bin/env python3

import socket
import json
import sys
import ipaddress
import signal
from notification_receiver import NotificationReceiver

def signal_handler(sig, frame):
    print('   Ctrl+C pressed, exiting.')
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

DEFAULT_IP = "::"
DEFAULT_PORT = 5678

if len(sys.argv) < 3:
    srv_addr = DEFAULT_IP
    srv_port = DEFAULT_PORT
    print("<IP address> and/or <port> not specfied in command line parameters, defaulting to ALL interafces and ",DEFAULT_PORT," port")
else:
    srv_addr = sys.argv[1]
    srv_port = int(sys.argv[2])

ip_version = "dual"

try:
    socket.inet_pton(socket.AF_INET, srv_addr)
    ip_version = "v4"
except:
    try:
        socket.inet_pton(socket.AF_INET6, srv_addr)
        ip_version = "v6"
    except:
        ip_version = "invalid"


if ip_version == "invalid" or srv_port < 1024 or srv_port > 65535 :
    print("invalid IP address / port: ",srv_addr," ",srv_port)
    exit(1)

if ip_version == "v4":
    sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
else: # v6 or dual
    sock = socket.socket(socket.AF_INET6,socket.SOCK_DGRAM)

sock.bind((srv_addr, srv_port))

receiver = NotificationReceiver()

print(f"JSON receiver server started on {srv_addr} : {srv_port}")



while True:
    data, addr = sock.recvfrom(4096)
    (host_ip,port) = socket.getnameinfo(addr,socket.NI_NUMERICHOST | socket.NI_NUMERICSERV)

    json_received = receiver.process_notification(host_ip, port, data)
    if json_received is not None:
        hostname = json_received.get("notif_hostname")
        seq_num = json_received.get("notif_seq")
        print(f'\nReceived {len(data)} bytes from {hostname} , {host_ip} : {port} with sequence number {seq_num}')
        print('========== JSON data begin ==========')
        print(json.dumps(json_received, indent=2))
        print('........... JSON data end ...........')
