import socket
import sys

UDP_IP = sys.argv[1]
UDP_PORT = 20345
MESSAGE = b"Hello, World!"
  
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(MESSAGE, (UDP_IP, UDP_PORT))

sock.close()