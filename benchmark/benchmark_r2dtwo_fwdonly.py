#!/usr/bin/python3
import sys
sys.path.append("../test")
from mininet.net import Mininet
from mininet.node import OVSController, OVSSwitch
from threading import Thread
from mininet.cli import CLI
from select import *
from utils import *
from pprint import pprint
import regex as re
import time
import json
from mininet_tools import *
from netbench_tools import *

def start_r2dtwos(net, debug, topo):
    # start R2DTWOs
    match topo:
        case "1hop" : nodelist = ['n']
        case "2hop" : nodelist = ['n1', 'n2']
    for n in nodelist:
        node = net.get(n)
        if debug:
            # For debug! Spawns r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo config/fwd_{topo}.ini")
        else:
            node.popen(f"../r2dtwo config/fwd_{topo}.ini -vALL:NONE")

def main():
    all_ok = False
    try:
        debug = False
        if len(sys.argv) >= 2 and sys.argv[1] == "debug":
            debug = True
            sys.argv.pop(1)
        if debug:
            print("Mininet benchmark debug")
        else:
            print("Mininet benchmark test")


        (topo,iperf_ver) = parse_cmdline()

        net_type = topo

        net = create_net(topo)
        config_net_fw(net)
        config_net_mtu(net,topo)

        start_r2dtwos(net, debug, topo)
        time.sleep(2)

        if debug:
            CLI(net)
        else:
            all_ok = run_tests(net,iperf_ver)
    except Exception as ex:
        print(type(ex))
    finally:
        net.stop()
        if all_ok:
            exit(0)
        exit(1)

if __name__ == "__main__":
    main()
