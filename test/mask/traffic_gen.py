#!/usr/bin/python3

from time import sleep
from socket import *

# simple traffic generator
# 100PPS 100bytes packets sent to localhost 5555 UDP port

def main():
    try:
        # 100 bytes of zero
        data = b'\x00' * 100
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        while True:
            sleep(0.01)
            sock.sendto(data, ("127.0.0.1", 5555))
    except KeyboardInterrupt:
        print("Interrupted by user...")
    finally:
        print("Traffic generator exiting...")
        sock.close()

main()
