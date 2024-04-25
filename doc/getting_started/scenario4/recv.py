import socket
import sys

UDP_IP = sys.argv[1]
UDP_PORT = 20345

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

try:
    sock.bind((UDP_IP, UDP_PORT))
    print("Listening on Port:", UDP_PORT)
except Exception:
    print("ERROR: Cannot connect to Port:", UDP_PORT)

try:
    while True:
        data, addr = sock.recvfrom(1024)
        print("received message: %s" % data)
except KeyboardInterrupt:
    sock.close()