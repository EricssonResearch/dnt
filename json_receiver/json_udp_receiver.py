import socket
import json
import sys
import ipaddress
import signal

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

print("JSON receiver server started")

SEQ_HISTORY_SIZE = 10
index=0
last_seqnums=[0] * SEQ_HISTORY_SIZE

supress = False

while True:
    data, addr = sock.recvfrom(2048) #gets UDP data up to 2kB
    (host,port) = socket.getnameinfo(addr,socket.NI_NUMERICHOST | socket.NI_NUMERICSERV)

    print('\nConnection from: ',host)
    try:
        jsonReceived = json.loads(data)    
        seq_num = jsonReceived.get("notif_seq")
        if seq_num != None:
            if (host, seq_num) in last_seqnums: 
                print("Message with sequence number ", seq_num, " already received, not showing the replica")
                supress = True
            else:
                last_seqnums[index] = (host, seq_num)
                index = ( index + 1 ) % SEQ_HISTORY_SIZE

        if not supress:
            print('========== JSON data begin ==========')
            print(json.dumps(jsonReceived, indent=2))
            print('=========== JSON data end ===========')
        supress = False

    except json.decoder.JSONDecodeError:
        print ("JSON decoder error: received message is not JSON or UDP receive buffer is too small to fit all the JSON data")
    

