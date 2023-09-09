#!/usr/bin/python3

from socket import AF_INET, SOCK_STREAM, SocketType, socket
from mininet.net import Mininet
from threading import Thread
from mininet.cli import CLI
from select import *
from utils import *
import regex as re
import time
import json


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

def start_r2dtwos(net):
    # start R2DTWOs
    for n in ['n1', 'n2', 'n3', 'n4']:
        node = net.get(n)
        node.popen(f"../r2dtwo oam/singlestage/{n}.cfg")
        # For debug! Spawns 4 r2dtwo windows in gdb
        # node.popen(f"xterm -T {n} -e gdb --args ../r2dtwo oam/singlestage/{n}.cfg")

# list of (sender, message, [expected JSON replies])
# The sender 'node' sending 'message' from telnet and expect the list of replies
testcases = [
    ('n1', 'ping s1:mepn1s1 in12 4',
     ['OAM packet ping session 1 seq 0, s1:mepn1s1 -> in12, level 4, count 1, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n'
      '{"label":23,"level":4,"node":"in12","nodeid":1,"request":"ping","sequence":0,"session":1,"stream":"s1","target":"in12"}']),

    ('n1', 'ping s1:mepn1s1 in23 4',
     ['OAM packet ping session 2 seq 0, s1:mepn1s1 -> in23, level 4, count 1, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":23,"level":4,"node":"in23","nodeid":1,"request":"ping","send_ns":206669393,"send_s":1691060339,"sequence":0,"session":2,"stream":"s1","target":"in23"}']),

    ('n1', 'ping s1:mepn1s1 in34 4',
     ['OAM packet ping session 3 seq 0, s1:mepn1s1 -> in34, level 4, count 1, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":34,"level":4,"node":"in34","nodeid":1,"request":"ping","send_ns":62649668,"send_s":1691060354,"sequence":0,"session":3,"stream":"s1","target":"in34"}']),

    ('n1', 'ping s1:mepn1s1 mepn4s1 4 -o',
     ['OAM packet ping session 4 seq 0, s1:mepn1s1 -> mepn4s1, level 4, count 1, rr: no os: yes\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":34,"level":4,"node":"mepn4s1","nodeid":1,"objects":{"discarded_packets":7,"history":"00","history_length":2,"latent_error_paths":2,"latent_error_resets":68807,' +
      '"latent_errors":0,"name":"pef4","passed_packets":7,"recovery_algorithm":"vector","recovery_seq_num":65535,"reset_msec":2000,"seq_recovery_resets":5,"type":"seqrec"' +
      ',"use_init_flag":false,"use_reset_flag":false},"request":"ping","send_ns":694512184,"send_s":1691060361,"sequence":0,"session":4,"stream":"s1","target":"mepn4s1"}']),

    ('n1', 'ping s1:mepn1s1 any 4',
     ['OAM packet ping session 5 seq 0, s1:mepn1s1 -> any, level 4, count 1, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":12,"level":4,"node":"in12","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}',
      '{"label":23,"level":4,"node":"in23","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}',
      '{"label":34,"level":4,"node":"out34","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}',
      '{"label":34,"level":4,"node":"in34","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}',
      '{"label":34,"level":4,"node":"mepn4s1","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}',
      '{"label":13,"level":4,"node":"in13","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}',
      '{"label":24,"level":4,"node":"in24","nodeid":1,"request":"ping","send_ns":146399386,"send_s":1691060381,"sequence":0,"session":5,"stream":"s1","target":"any"}']),

    ('n1', 'ping s1:mepn1s1 in24 4 -r',
     ['OAM packet ping session 6 seq 0, s1:mepn1s1 -> in24, level 4, count 1, rr: yes os: no\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":24,"level":4,"node":"in24","nodeid":1,"request":"ping","rr":["in24","in12","s1:mepn1s1"],"send_ns":682073488,"send_s":1691060394,"sequence":0,"session":6,"stream":"s1","target":"in24"}']),

    ('n1', 'ping s1:mepn1s1 mepn4s1 4 -r',
     ['OAM packet ping session 7 seq 0, s1:mepn1s1 -> mepn4s1, level 4, count 1, rr: yes os: no\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":34,"level":4,"node":"mepn4s1","nodeid":1,"request":"ping","rr":["mepn4s1","in34","out34","in23","in12","s1:mepn1s1"],"send_ns":361302725,"send_s":1691060420,"sequence":0,"session":7,"stream":"s1","target":"mepn4s1"}']),

    ('n1', 'rping rx12:in12 s1:mepn1s1 mepn4s1 4',
     ['OAM packet rping session 8 seq 0, s1:mepn1s1 -> mepn4s1, level 4, count 1 interval 1000, rr: no os: no\t[reply to ip: 10.0.0.1, port: 6634]\n',
      '{"label":344,"level":4,"node":"mepn4s1","nodeid":2,"request":"ping","send_ns":133040181,"send_s":1694296246,"sequence":0,"session":1,"stream":"rx12","target":"mepn4s1"}']),

]

def ordered(obj):
    if isinstance(obj, dict):
        return sorted((k, ordered(v)) for k, v in obj.items())
    if isinstance(obj, list):
        return sorted(ordered(x) for x in obj)
    else:
        return obj

def cmp_json(j1s: str, j2s : str):
    # print('*', j1s, '*')
    # print('#', j2s, '#')
    j1 = json.loads(j1s)
    j2 = json.loads(j2s)
    for j in [j1, j2]:
        del j["send_s"]
        del j["send_ns"]
        if "objects" in j:
            del j["objects"] #TODO: rethink
    return ordered(j1) == ordered(j2)

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
                s.settimeout(1)
                s.send(msg.encode())
                replies = []
                for reply_part in expected_reply:
                    reply = s.recv(10000).decode()
                    replies.append(reply)
                print(f"Node: {node}, command: {msg}", end=" ")

                all_ok = True
                all_ok == all_ok and (replies[0] == expected_reply[0])
                for i, actual in enumerate(replies[1:]):
                    all_oks = all_ok and cmp_json(actual, expected_reply[i + 1])
#                    act_list = actual.split('\n')
#                    exp_list = expected_reply[i + 1] #.split('\n')
#                    for act, exp in zip(act_list, exp_list):
#                        all_ok = all_ok and cmp_json(act, exp)
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
        print("R2DTWO OAM test")
        net = create_net()
        config_net(net)
        start_r2dtwos(net)
        # CLI(net)
        run_tests(net, testcases)
    finally:
        print("Cleanup...")
        exec_fg("killall r2dtwo")
        #exec_fg("killall gdb")
        net.stop()

if __name__ == "__main__":
    main()
