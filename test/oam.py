#!/usr/bin/python3

from socket import AF_INET, SOCK_STREAM, SocketType, socket
from mininet.net import Mininet
from threading import Thread
from mininet.cli import CLI
from select import *
from utils import *
from pprint import pprint
import regex as re
import time
import json
import sys


def create_net():
    """
    For single- and multi-stage PREOF OAM testing.
                      ---n2---
                     /   ||   \
        talker ---- n1---n3---n4----listener
    192.168.1.1                    192.168.1.2

    Router-local IPs for OAM: 10.0.0.{1,2,3,4}/32
    """
    net = Mininet(autoStaticArp=True)

    # nodes: a, b, c, d, talker, listener
    talker = net.addHost('talker', ip='192.168.1.1/24')
    listener =  net.addHost('listener', ip='192.168.1.2/24')
    n1 = net.addHost('n1', ip=None)
    n2 = net.addHost('n2', ip=None)
    n3 = net.addHost('n3', ip=None)
    n4 = net.addHost('n4', ip=None)

    # links
    net.addLink(talker, n1, intfName1='eth0', intfName2='eth0')
    net.addLink(n1, n2, intfName1='eth12', intfName2='eth21')
    net.addLink(n1, n3, intfName1='eth13', intfName2='eth31')
    net.addLink(n2, n3, intfName1='eth23', intfName2='eth32')
    # Uncomment line below for multi-stage PREOF network
    # net.addLink(n2, n3, intfName1='eth230', intfName2='eth320') #redundant
    net.addLink(n2, n4, intfName1='eth24', intfName2='eth42')
    net.addLink(n3, n4, intfName1='eth34', intfName2='eth43')
    net.addLink(listener, n4, intfName1='eth0', intfName2='eth0')

    net.build()
    return net

def config_net(net):
    t, l, n1, n2, n3, n4 = [net.get(n) for n in ["talker", "listener", "n1", "n2", "n3", "n4"]]
    ip_lo = 1
    for n in [n1, n2, n3, n4]:
        n.cmd(f"ip a a 10.0.0.{ip_lo}/32 dev lo")
        n.cmd("sysctl -w net.ipv4.ip_forward=1")
        ip_lo += 1
    # t.cmd("ip a a 192.168.1.1/24 dev eth0")
    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")
    # l.cmd("ip a a 192.168.1.2/24 dev eth0")
    n1.cmd("ip a a 12.0.0.1/24 dev eth12")
    n1.cmd("ip a a 13.0.0.1/24 dev eth13")
    n2.cmd("ip a a 12.0.0.2/24 dev eth21")
    n2.cmd("ip a a 23.0.0.2/24 dev eth23")
    n2.cmd("ip a a 24.0.0.2/24 dev eth24")
    n3.cmd("ip a a 13.0.0.3/24 dev eth31")
    n3.cmd("ip a a 23.0.0.3/24 dev eth32")
    n3.cmd("ip a a 34.0.0.3/24 dev eth34")
    n4.cmd("ip a a 24.0.0.4/24 dev eth42")
    n4.cmd("ip a a 34.0.0.4/24 dev eth43")

    # routing
    n1.cmd("ip r a 24.0.0.0/24 via 12.0.0.2 metric 5")
    n1.cmd("ip r a 24.0.0.0/24 via 13.0.0.3 metric 10")
    n1.cmd("ip r a 34.0.0.0/24 via 13.0.0.3 metric 5")
    n1.cmd("ip r a 34.0.0.0/24 via 12.0.0.2 metric 10")
    n3.cmd("ip r a 24.0.0.0/24 via 23.0.0.2 metric 5")
    n4.cmd("ip r a 12.0.0.0/24 via 24.0.0.2 metric 5")
    n4.cmd("ip r a 12.0.0.0/24 via 34.0.0.3 metric 10")
    n4.cmd("ip r a 13.0.0.0/24 via 34.0.0.3 metric 5")
    n4.cmd("ip r a 13.0.0.0/24 via 24.0.0.2 metric 10")

    # for OAM router-local address availability
    n1.cmd("ip r a default via 12.0.0.2")
    n2.cmd("ip r a default via 24.0.0.4")
    n3.cmd("ip r a default via 13.0.0.1")
    n4.cmd("ip r a default via 34.0.0.3")
    n2.cmd("ip r add 10.0.0.1/32 via 12.0.0.1")
    n3.cmd("ip r add 10.0.0.1/32 via 13.0.0.1")
    n4.cmd("ip r add 10.0.0.1/32 via 34.0.0.3")


    # delay
    n1.cmd("tc qdisc add dev eth13 root netem delay 20ms")
    n1.cmd("tc qdisc add dev eth12 root netem delay 1ms")
    n2.cmd("tc qdisc add dev eth24 root netem delay 20ms")
    n2.cmd("tc qdisc add dev eth23 root netem delay 2ms")
    n2.cmd("tc qdisc add dev eth21 root netem delay 2ms")
    n3.cmd("tc qdisc add dev eth31 root netem delay 3ms")
    n4.cmd("tc qdisc add dev eth43 root netem delay 4ms")
    # n3.cmd("tc qdisc add dev eth34 root netem delay 10ms")

def reply_printer(sock: SocketType):
    # Use it by calling the followings:
    # t = Thread(target=reply_printer, args=[s])
    # t.start()
    try:
        sock.setblocking(0)
        while True:
            r, _, _ = select([sock], [], [], 0.1)
            for x in r:
                # print("select returned")
                msg = x.recv(10000)
                if not msg:
                    continue
                print(msg.decode())
    finally:
        print("End")

def start_r2dtwos(net, debug):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        if debug:
            # For debug! Spawns 4 r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo oam/singlestage/{n}.cfg")
        else:
            node.popen(f"../r2dtwo oam/singlestage/{n}.cfg")

# list of (sender, message, [expected JSON replies])
# The sender 'node' sending 'message' from telnet and expect the list of replies
testcases = [
    ('n1', 'ping s1:s1n1-e4-01 s1n2-i3-12 3',
        ['OAM packet ping session 2 seq 0, s1:s1n1-e4-01 -> s1n2-i3-12, level 3, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":112,"level":3,"nodeid":1,"receiver":"s1n2-i3-12","recv_ns":449034649,"recv_s":1697613837,"code":"reply","type":"ping","send_ns":444746206,"send_s":1697613837,"sequence":0,"session":2,"stream":"s1","target":"s1n2-i3-12"}']
     ),

    ('n1', 'ping s1:s1n1-e4-01 s1n3-i4-23 4',
        ['OAM packet ping session 3 seq 0, s1:s1n1-e4-01 -> s1n3-i4-23, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":123,"level":4,"nodeid":1,"receiver":"s1n3-i4-23","recv_ns":201952480,"recv_s":1697613838,"code":"reply","type":"ping","send_ns":198683264,"send_s":1697613838,"sequence":0,"session":3,"stream":"s1","target":"s1n3-i4-23"}']
     ),

    ('n1', 'ping s1:s1n1-e4-01 s1n4-i4-34 4',
        ['OAM packet ping session 4 seq 0, s1:s1n1-e4-01 -> s1n4-i4-34, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":134,"level":4,"nodeid":1,"receiver":"s1n4-i4-34","recv_ns":657490576,"recv_s":1697614354,"code":"reply","type":"ping","send_ns":654306224,"send_s":1697614354,"sequence":0,"session":4,"stream":"s1","target":"s1n4-i4-34"}']
     ),

    ('n1', 'ping s1:s1n1-e4-01 s1n4-e4-40 4 -o',
        ['OAM packet ping session 5 seq 0, s1:s1n1-e4-01 -> s1n4-e4-40, level 4, count 1 interval 1000, rr: no os: yes\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":134,"level":4,"nodeid":1,"object":{"discarded_packets":5,"history":"11","history_length":2,"latent_error_paths":2,"latent_error_resets":2263,"latent_errors":0,"name":"pef4","passed_packets":5,"recovery_algorithm":"vector","recovery_seq_num":4,"reset_msec":2000,"seq_recovery_resets":0,"type":"seqrec","use_init_flag":false,"use_reset_flag":false},"receiver":"s1n4-e4-40","recv_ns":226042168,"recv_s":1697614462,"code":"reply","type":"ping","send_ns":222808040,"send_s":1697614462,"sequence":0,"session":5,"stream":"s1","target":"s1n4-e4-40"}']
     ),

    ('n1', 'ping s1:s1n1-e4-01 any 4',
        ['OAM packet ping session 6 seq 0, s1:s1n1-e4-01 -> any, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"code":"reply","label":123,"level":4,"nodeid":1,"receiver":"s1n3-i4-23","recv_ns":159531731,"recv_s":1701690346,"send_ns":156420426,"send_s":1701690346,"sequence":0,"session":6,"stream":"s1","target":"any","type":"ping"}\n{"code":"reply","label":123,"level":4,"nodeid":1,"receiver":"s1n3-i4-34","recv_ns":159531731,"recv_s":1701690346,"send_ns":156420426,"send_s":1701690346,"sequence":0,"session":6,"stream":"s1","target":"any","type":"ping"}\n{"code":"reply","label":134,"level":4,"nodeid":1,"receiver":"s1n4-i4-34","recv_ns":159690141,"recv_s":1701690346,"send_ns":156420426,"send_s":1701690346,"sequence":0,"session":6,"stream":"s1","target":"any","type":"ping"}\n{"code":"reply","label":134,"level":4,"nodeid":1,"receiver":"s1n4-e4-40","recv_ns":159690141,"recv_s":1701690346,"send_ns":156420426,"send_s":1701690346,"sequence":0,"session":6,"stream":"s1","target":"any","type":"ping"}\n{"code":"reply","label":113,"level":4,"nodeid":1,"receiver":"s1n3-i4-13","recv_ns":176452358,"recv_s":1701690346,"send_ns":156420426,"send_s":1701690346,"sequence":0,"session":6,"stream":"s1","target":"any","type":"ping"}\n{"code":"reply","label":124,"level":4,"nodeid":1,"receiver":"s1n4-i4-24","recv_ns":177555973,"recv_s":1701690346,"send_ns":156420426,"send_s":1701690346,"sequence":0,"session":6,"stream":"s1","target":"any","type":"ping"}\n']
        # ['OAM packet ping session 6 seq 0, s1:s1n1-e4-01 -> any, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":123,"level":4,"nodeid":1,"receiver":"s1n3-i4-23","recv_ns":170770283,"recv_s":1697614356,"code":"reply","type":"ping","send_ns":167648581,"send_s":1697614356,"sequence":0,"session":6,"stream":"s1","target":"any"}{"label":123,"level":4,"nodeid":1,"receiver":"s1n3-i4-34","recv_ns":170770283,"recv_s":1697614356,"code":"reply","type":"ping","send_ns":167648581,"send_s":1697614356,"sequence":0,"session":6,"stream":"s1","target":"any"}{"label":134,"level":4,"nodeid":1,"receiver":"s1n4-i4-34","recv_ns":171057714,"recv_s":1697614356,"code":"reply","type":"ping","send_ns":167648581,"send_s":1697614356,"sequence":0,"session":6,"stream":"s1","target":"any"}{"label":134,"level":4,"nodeid":1,"receiver":"s1n4-e4-40","recv_ns":171057714,"recv_s":1697614356,"code":"reply","type":"ping","send_ns":167648581,"send_s":1697614356,"sequence":0,"session":6,"stream":"s1","target":"any"}{"label":113,"level":4,"nodeid":1,"receiver":"s1n3-i4-13","recv_ns":187699738,"recv_s":1697614356,"code":"reply","type":"ping","send_ns":167648581,"send_s":1697614356,"sequence":0,"session":6,"stream":"s1","target":"any"}{"label":124,"level":4,"nodeid":1,"receiver":"s1n4-i4-24","recv_ns":188775260,"recv_s":1697614356,"code":"reply","type":"ping","send_ns":167648581,"send_s":1697614356,"sequence":0,"session":6,"stream":"s1","target":"any"}']
     ),

    ('n1', 'ping s1:s1n1-e4-01 s1n4-i4-24 4 -r',
        ['OAM packet ping session 7 seq 0, s1:s1n1-e4-01 -> s1n4-i4-24, level 4, count 1 interval 1000, rr: yes os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":124,"level":4,"nodeid":1,"receiver":"s1n4-i4-24","recv_ns":943982537,"recv_s":1697614356,"code":"reply","type":"ping","rr":["s1n4-i4-24","s1:s1n1-e4-01"],"send_ns":922851702,"send_s":1697614356,"sequence":0,"session":7,"stream":"s1","target":"s1n4-i4-24"}']
     ),

    ('n1', 'ping s1:s1n1-e4-01 s1n4-e4-40 4 -r',
        ['OAM packet ping session 8 seq 0, s1:s1n1-e4-01 -> s1n4-e4-40, level 4, count 1 interval 1000, rr: yes os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":134,"level":4,"nodeid":1,"receiver":"s1n4-e4-40","recv_ns":686240445,"recv_s":1697614357,"code":"reply","type":"ping","rr":["s1n4-e4-40","s1n4-i4-34","s1n3-i4-34","s1n3-i4-23","s1:s1n1-e4-01"],"send_ns":683039091,"send_s":1697614357,"sequence":0,"session":8,"stream":"s1","target":"s1n4-e4-40"}']
    ),

    (
        'n1', 'rlist s1:s1n1-e4-01 any 4',
        ['OAM packet rlist session 9 seq 0, s1:s1n1-e4-01 -> any, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', 'Rlist result from s1n3-i4-23:\nrx123:s1n3-i4-23\nrx123:s1n3-i4-34\n\nRlist result from s1n3-i4-34:\nrx123:s1n3-i4-23\nrx123:s1n3-i4-34\n\nRlist result from s1n4-i4-34:\nrx134:s1n4-i4-34\n\nRlist result from s1n4-e4-40:\nrx134:s1n4-i4-34\n\nRlist result from s1n3-i4-13:\nrx113:s1n3-i4-34\nrx113:s1n3-i4-13\n\nRlist result from s1n4-i4-24:\nrx124:s1n4-i4-24\n\n']
        # ['OAM packet rlist session 9 seq 0, s1:s1n1-e4-01 -> any, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', 'Rlist result from s1n3-i4-23:\nrx123:s1n3-i4-23\nrx123:s1n3-i4-34\nRlist result from s1n3-i4-34:\nrx123:s1n3-i4-23\nrx123:s1n3-i4-34\nRlist result from s1n4-i4-34:\nrx134:s1n4-i4-34\nRlist result from s1n4-e4-40:\nrx134:s1n4-i4-34\nRlist result from s1n3-i4-13:\nrx113:s1n3-i4-34\nrx113:s1n3-i4-13\nRlist result from s1n4-i4-24:\nrx124:s1n4-i4-24\n']
    ),

    (
        'n1', 'rlist s1:s1n1-e4-01 any 3',
        # ['OAM packet rlist session 10 seq 0, s1:s1n1-e4-01 -> any, level 3, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', 'Rlist result from s1n2-i3-12:\nrx12:s1n2-i3-12\nRlist result from s1n3-e3-23:\nrx123:s1n3-i4-23\nrx123:s1n3-i4-34\nRlist result from s1n4-e3-24:\nrx124:s1n4-i4-24\n']
        ['OAM packet rlist session 10 seq 0, s1:s1n1-e4-01 -> any, level 3, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', 'Rlist result from s1n2-i3-12:\nrx12:s1n2-i3-12\n\nRlist result from s1n3-e3-23:\nrx123:s1n3-i4-23\nrx123:s1n3-i4-34\n\nRlist result from s1n4-e3-24:\nrx124:s1n4-i4-24\n\n']
    ),
    (
        'n1', 'rping s1:s1n1-e4-01 s1n3-i4-13 4 rx113:s1n3-i4-13 any 4',
        # ['','OAM packet rping session 11 seq 0, s1:s1n1-e4-01 -> s1n3-i4-13, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":0,"level":4,"nodeid":3,"receiver":"s1n3-i4-34","recv_ns":678618538,"recv_s":1697633586,"code":"reply","type":"ping","send_ns":678618538,"send_s":1697633586,"sequence":0,"session":11,"stream":"s1","target":"any"}{"label":134,"level":4,"nodeid":3,"receiver":"s1n4-i4-34","recv_ns":678748215,"recv_s":1697633586,"code":"reply","type":"ping","send_ns":678618538,"send_s":1697633586,"sequence":0,"session":11,"stream":"s1","target":"any"}{"label":134,"level":4,"nodeid":3,"receiver":"s1n4-e4-40","recv_ns":678748215,"recv_s":1697633586,"code":"reply","type":"ping","send_ns":678618538,"send_s":1697633586,"sequence":0,"session":11,"stream":"s1","target":"any"}']
        ['OAM packet rping session 11 seq 0, s1:s1n1-e4-01 -> s1n3-i4-13, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"code":"reply","label":0,"level":4,"nodeid":3,"receiver":"s1n3-i4-34","recv_ns":118961213,"recv_s":1701692855,"send_ns":118961213,"send_s":1701692855,"sequence":0,"session":11,"stream":"s1","target":"any","type":"ping"}\n']
    ),

    (
        'n1', 'rping s1:s1n1-e4-01 s1n3-i4-13 4 rx113:s1n3-i4-34 any 4',
        # ['OAM packet rping session 12 seq 0, s1:s1n1-e4-01 -> s1n3-i4-13, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":134,"level":4,"nodeid":3,"receiver":"s1n4-i4-34","recv_ns":38885566,"recv_s":1697615245,"code":"reply","type":"ping","send_ns":38868328,"send_s":1697615245,"sequence":0,"session":12,"stream":"s1","target":"any"}{"label":134,"level":4,"nodeid":3,"receiver":"s1n4-e4-40","recv_ns":38885566,"recv_s":1697615245,"code":"reply","type":"ping","send_ns":38868328,"send_s":1697615245,"sequence":0,"session":12,"stream":"s1","target":"any"}']
        ['OAM packet rping session 12 seq 0, s1:s1n1-e4-01 -> s1n3-i4-13, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"code":"reply","label":134,"level":4,"nodeid":3,"receiver":"s1n4-i4-34","recv_ns":466601107,"recv_s":1701692934,"send_ns":466576723,"send_s":1701692934,"sequence":0,"session":12,"stream":"s1","target":"any","type":"ping"}\n{"code":"reply","label":134,"level":4,"nodeid":3,"receiver":"s1n4-e4-40","recv_ns":466601107,"recv_s":1701692934,"send_ns":466576723,"send_s":1701692934,"sequence":0,"session":12,"stream":"s1","target":"any","type":"ping"}\n']
    ),

    (
        'n1', 'rping s1:s1n1-e4-01 s1n3-i4-34 4 rx113:s1n3-i4-13 any 4',
        ['OAM packet rping session 13 seq 0, s1:s1n1-e4-01 -> s1n3-i4-34, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"code":"reply","label":0,"level":4,"nodeid":3,"receiver":"s1n3-i4-34","recv_ns":206268791,"recv_s":1701692935,"send_ns":206268791,"send_s":1701692935,"sequence":0,"session":13,"stream":"s1","target":"any","type":"ping"}\n']
    ),

    (
        'n1', 'rping s1:s1n1-e4-01 s1n3-i4-34 4 rx113:s1n3-i4-34 any 4',
        ['OAM packet rping session 14 seq 0, s1:s1n1-e4-01 -> s1n3-i4-34, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"code":"reply","label":134,"level":4,"nodeid":3,"receiver":"s1n4-i4-34","recv_ns":651263294,"recv_s":1698225601,"send_ns":651246899,"send_s":1698225601,"sequence":0,"session":14,"stream":"s1","target":"any","type":"ping"}{"code":"reply","label":134,"level":4,"nodeid":3,"receiver":"s1n4-e4-40","recv_ns":651263294,"recv_s":1698225601,"send_ns":651246899,"send_s":1698225601,"sequence":0,"session":14,"stream":"s1","target":"any","type":"ping"}']
    ),
    (
        'n4', 'ping s2:s2n4-e5-04 s2n1-i5-21 5',
        ['OAM packet ping session 2 seq 0, s2:s2n4-e5-04 -> s2n1-i5-21, level 5, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.4, port: 6634]\n', '{"label":242,"level":5,"nodeid":4,"receiver":"s2n1-i5-21","recv_ns":92055282,"recv_s":1697635715,"code":"reply","type":"ping","send_ns":89965700,"send_s":1697635715,"sequence":0,"session":2,"stream":"s2","target":"s2n1-i5-21"}']
    ),
    (
        'n4', 'ping s2:s2n4-e5-04 s2n1-i5-31 5',
        ['OAM packet ping session 3 seq 0, s2:s2n4-e5-04 -> s2n1-i5-31, level 5, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.4, port: 6634]\n', '{"label":243,"level":5,"nodeid":4,"receiver":"s2n1-i5-31","recv_ns":858900808,"recv_s":1697635715,"code":"reply","type":"ping","send_ns":851822013,"send_s":1697635715,"sequence":0,"session":3,"stream":"s2","target":"s2n1-i5-31"}']
    ),
    (
        'n4', 'ping s2:s2n4-e5-04 s2n1-e5-10 5',
        ['OAM packet ping session 4 seq 0, s2:s2n4-e5-04 -> s2n1-e5-10, level 5, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.4, port: 6634]\n', '{"label":242,"level":5,"nodeid":4,"receiver":"s2n1-e5-10","recv_ns":626887770,"recv_s":1697635716,"code":"reply","type":"ping","send_ns":624830507,"send_s":1697635716,"sequence":0,"session":4,"stream":"s2","target":"s2n1-e5-10"}']
    ),

    (
        'n1', 'ping s3:s3n1-e4-01 any 4',
        ['OAM packet ping session 2 seq 0, s3:s3n1-e4-01 -> any, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n', '{"label":313,"level":4,"nodeid":1,"receiver":"s3n3-i4-13","recv_ns":155097178,"recv_s":1697638268,"code":"reply","type":"ping","send_ns":135054059,"send_s":1697638268,"sequence":0,"session":2,"stream":"s3","target":"any"}{"label":313,"level":4,"nodeid":1,"receiver":"s3n4-i4-34","recv_ns":155343515,"recv_s":1697638268,"code":"reply","type":"ping","send_ns":135054059,"send_s":1697638268,"sequence":0,"session":2,"stream":"s3","target":"any"}{"label":313,"level":4,"nodeid":1,"receiver":"s3n4-e4-40","recv_ns":155343515,"recv_s":1697638268,"code":"reply","type":"ping","send_ns":135054059,"send_s":1697638268,"sequence":0,"session":2,"stream":"s3","target":"any"}', '{"label":313,"level":4,"nodeid":1,"receiver":"s3n4-i4-24","recv_ns":175347610,"recv_s":1697638268,"code":"reply","type":"ping","send_ns":135054059,"send_s":1697638268,"sequence":0,"session":2,"stream":"s3","target":"any"}']
    ),
    (
        'n3', 'ping tx332:s3n3-e1-32 any 1',
        ['OAM packet ping session 1 seq 0, tx332:s3n3-e1-32 -> any, level 1, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.3, port: 6634]\n', '{"label":313,"level":1,"nodeid":3,"receiver":"s3n4-e1-24","recv_ns":851261611,"recv_s":1697636726,"code":"reply","type":"ping","send_ns":831211149,"send_s":1697636726,"sequence":0,"session":1,"stream":"tx332","target":"any"}']
    ),
]

def patch_json(jsstr):
    # TODO: this can be fixed in the JSON dump func in r2dtwo
    # with properly dumping multiple responses (e.g. ping ... any)
    if "}{" in jsstr or '}\n{' in jsstr:
        jsstr = jsstr.replace("}{", "},{")
        jsstr = jsstr.replace("}\n{", "},{")
        jsstr = "[" + jsstr + "]"
        return jsstr
    return jsstr

# from: https://stackoverflow.com/questions/27838319/python-delete-all-specific-keys-in-json
def del_all(d):
    """
    Delete all alternating values like send/recv times, or object
    """
    if not isinstance(d, (dict, list)):
        return d
    if isinstance(d, list):
        return [ del_all(v) for v in d]
    return {k: del_all(v) for k, v in d.items()
            if k not in {'send_s', 'send_ns', 'recv_s', 'recv_ns', 'object'}}

def cmp_json(j1s: str, j2s : str):
    j1 = json.loads(patch_json(j1s))
    j2 = json.loads(patch_json(j2s))
    j1 = del_all(j1)
    j2 = del_all(j2)
    return j1 == j2

def prepare_cli(sock):
    _ = sock.recv(10000) # OAM ready
    sock.send("mode json\n".encode())
    _ = sock.recv(10000) # JSON mode ack


def run_tests(net, test):
    raddrs = {
        'n1' : "10.0.0.1",
        'n2' : "10.0.0.2",
        'n3' : "10.0.0.3",
        'n4' : "10.0.0.4",
    }
    success = 0
    for node, msg, expected_reply in test:
        time.sleep(0.2)
        switch_netns(node)
        with socket(AF_INET, SOCK_STREAM, 0) as s:
            try:
                s.connect((raddrs[node], 8000))
                prepare_cli(s)
                s.settimeout(3)
                s.send(msg.encode())
                replies = []
                for reply_part in expected_reply:
                    reply = s.recv(10000).decode()
                    replies.append(reply)
                print(f"Node: {node}, command: {msg}", end=" ")

                all_ok = True
                all_ok = all_ok and (replies[0] == expected_reply[0])
                for i, actual in enumerate(replies[1:]):
                    if "rlist" in msg:
                        all_ok = all_ok and actual == expected_reply[i + 1]
                    else:
                        all_ok = all_ok and cmp_json(actual, expected_reply[i + 1])
                if all_ok:
                    print("✔")
                    success += 1
                else:
                    print("✘ FAILED: OAM reply different")
                    print(f"Actual reply:\n{replies}\nExpected reply:\n{expected_reply}\n")
            except Exception:
                print("FAILED: OAM reply parts missing")
                print(f"Actual reply:\n{replies}\nExpected reply:\n{expected_reply}\n")
            finally:
                s.close()
        time.sleep(0.5)
    switch_netns()
    print(f"Successful tests: {success}/{len(test)}")

def main():
    try:
        debug = False
        if len(sys.argv) >= 2 and sys.argv[1] == "debug":
            debug = True
        if debug:
            print("R2DTWO OAM debug")
        else:
            print("R2DTWO OAM test")
        net = create_net()
        config_net(net)
        start_r2dtwos(net, debug)
        if debug:
            CLI(net)
        else:
            run_tests(net, testcases)
    finally:
        print("Cleanup...")
        exec_fg("killall r2dtwo")
        #exec_fg("killall gdb")
        net.stop()

if __name__ == "__main__":
    main()
