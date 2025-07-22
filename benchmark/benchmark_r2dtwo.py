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

def start_r2dtwos(net, debug, topo, mode):
    # start R2DTWOs
    match topo:
        case "1hop" : nodelist = ['n']
        case "2hop" : nodelist = ['n1', 'n2']
    for n in nodelist:
        node = net.get(n)
        if debug:
            # For debug! Spawns r2dtwo windows in gdb
            node.popen(f"xterm -T {n} -e env -i gdb -nx --args ../r2dtwo config/{mode}_{topo}.ini")
        else:
            node.popen(f"../r2dtwo config/{mode}_{topo}.ini -vALL:NONE")

def main():
    all_ok = False

    (ops, topo, iperf_ver, debug ) = parse_cmdline()

    try:
        if debug:
            print("Mininet benchmark debug")
        else:
            print("Mininet benchmark test")

        net = create_net(topo)
        config_net_fw(net)
        config_net_mtu(net,topo)

        start_r2dtwos(net, debug, topo, ops)

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
