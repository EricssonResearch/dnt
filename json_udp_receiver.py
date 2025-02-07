import socket
import json
import sys
import ipaddress
import signal

def signal_handler(sig, frame):
    print('   Ctrl+C pressed, exiting.')
    sys.exit(0)

def is_ipv4(string):
    try:
        ipaddress.IPv4Network(string)
        return True
    except ValueError:
        return False

signal.signal(signal.SIGINT, signal_handler)

DEFAULT_IP = "127.0.0.1"
DEFAULT_PORT = 5678

if len(sys.argv) < 3:
    ipaddr = DEFAULT_IP
    port = DEFAULT_PORT
    print("<IP address> and/or <port> not specfied in command line parameters, defaulting to ",DEFAULT_IP, " and ", DEFAULT_PORT)
else:
    ipaddr = sys.argv[1]
    port = int(sys.argv[2])

if not is_ipv4(ipaddr) or port < 1024 or port > 65535 :
    print("invalid IP address / port")
    exit(1)

sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

sock.bind((ipaddr, port))

print("JSON receiver server started")

while True:
    data, addr = sock.recvfrom(2048) #gets UDP data

    print('\nConnection from: ', addr)

    try:
        jsonReceived = json.loads(data) #sets UDP received as JSON    
        print('========== JSON data begin ==========')
        print(json.dumps(jsonReceived, indent=2))
        print('=========== JSON data end ===========')

    except json.decoder.JSONDecodeError:
        print ("JSON decoder error, UDP receive buffer is probably too small to fit all the sent data")
    

