#!/usr/bin/env python3

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

print(f"JSON receiver server started on {srv_addr} : {srv_port}")

SEQ_HISTORY_SIZE = 10
index=0
last_seqnums=[0] * SEQ_HISTORY_SIZE

supress = False
omit_fragment = False

multipart_message={}
multipart_last_seqnum = -1
frag_count = 0


while True:
    data, addr = sock.recvfrom(4096)
    (host_ip,port) = socket.getnameinfo(addr,socket.NI_NUMERICHOST | socket.NI_NUMERICSERV)

    try:
        jsonReceived = json.loads(data)
        seq_num = jsonReceived.get("notif_seq")
        hostname = jsonReceived.get("notif_hostname")
        frag_id = jsonReceived.get("notif_fragment")
        timestamp = jsonReceived.get("notif_tstamp")
        if ( frag_id == None  or frag_id == "1/1" ): # not multipart message
            if (hostname, port, seq_num, frag_id) in last_seqnums:
                print("Message with sequence number ", seq_num, "from ", hostname, " with fragment ID ", frag_id, " already received, not showing the replica")
                supress = True
            else:
                last_seqnums[index] = (hostname, port, seq_num, frag_id)
                index = ( index + 1 ) % SEQ_HISTORY_SIZE

            if not supress:
                print(f'\nReceived {len(data)} bytes from {hostname} , {host_ip} : {port} with sequence number {seq_num}')
                print('========== JSON data begin ==========')
                print(json.dumps(jsonReceived, indent=2))
                print('........... JSON data end ...........')
            supress = False
            
        else: # multipart message, first collect, then print
            idx = int(frag_id[0:frag_id.find('/')])
            items = int(frag_id[frag_id.find('/')+1:])
 
            if (hostname, port, seq_num, frag_id) in last_seqnums:
                print("Multipart message with sequence number ", seq_num, "from ", hostname, " with fragment ID ", frag_id, " already received, omitting replicated fragment")
                omit_fragment = True
            else:
                last_seqnums[index] = (hostname, port, seq_num, frag_id)
                index = ( index + 1 ) % SEQ_HISTORY_SIZE

            if (not omit_fragment):
                if (multipart_last_seqnum == -1 ):
                    multipart_last_seqnum = seq_num                

                # collecting message parts
                
                if (multipart_last_seqnum == seq_num ) :
                    frag_count += 1
                else:
                    frag_count = 1
                    multipart_message.clear()
                    

                multipart_message[idx] = jsonReceived.get("notif_msg")

                print ("idx= ", idx, " items= ", items, "seq= ", seq_num, "last_seq= ", multipart_last_seqnum, "frag_count= ", frag_count)

                # print("frag_count= ", frag_count)
                if ( frag_count == items ) : # all parts received
                    # assembling message parts and build the full message                   
                    concat_string=""
                    for i in range(1,items+1):
                        concat_string += multipart_message[i]
                    # print(concat_string)
                    fullmsg_with_header={}
                    fullmsg_with_header["notif_seq"] = seq_num
                    fullmsg_with_header["notif_hostname"] = hostname
                    fullmsg_with_header["notif_tstamp"] = timestamp
                    fullmsg_with_header["notif_msg"] = json.loads(concat_string)

                    print(f'\nReceived {len(concat_string)} bytes from {hostname} , {host_ip} : {port} with sequence number {seq_num}')
                    print('========== JSON data begin ==========')
                    print(json.dumps(fullmsg_with_header, indent=2))
                    #print(json.dumps(json.loads(concat_string), indent=2))
                    print('........... JSON data end ...........')
                    multipart_last_seqnum = -1
                    multipart_message.clear()
                    frag_count = 0

                elif (seq_num != multipart_last_seqnum ): # some parts are missing and a new seq_num was received
                    print(f'\nWARNING: Missing part(s) of multipart message from {hostname} , {host_ip} : {port} with sequence number {multipart_last_seqnum}')
                    # reset parts collection and counters
                    #multipart_message.clear()
                    #frag_count = 0
                    multipart_last_seqnum = seq_num

            omit_fragment = False

    except json.decoder.JSONDecodeError:
        print ("JSON decoder error: received message is not JSON or UDP receive buffer is too small to fit all the JSON data")


