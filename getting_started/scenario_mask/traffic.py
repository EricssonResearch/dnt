#!/usr/bin/python

from time import sleep
from socket import *
import sys

with socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) as sk:
    try:
        while not sleep(0.5):
            sk.sendto(b"Hello", (sys.argv[1], 5555))
    except KeyboardInterrupt:
        print("exiting...")
