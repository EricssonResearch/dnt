#!/usr/bin/python

from socket import *

eth = "aa aa aa aa aa aa bb bb bb bb bb bb 81 00 "
vlan = "c0 00 08 00 " # vlan header with PCP=6 (0xc0) and VID=0
ip = "45 00 00 54 c1 3c 40 00 40 01 65 6a 0a 00 00 01 0a 00 00 02 "
icmp = "08 00 f6 5a c7 8d 00 01 76 70 c8 66 00 00 00 00"
"2f 6c 0d 00 00 00 00 00 10 11 12 13 14 15 16 17"
"18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27"
"28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37"

packet = bytearray.fromhex(eth + vlan + ip + icmp)

sk = socket(AF_PACKET, SOCK_RAW, 0)
sk.bind(("eth0", 0))
sk.send(packet)
sk.close()
