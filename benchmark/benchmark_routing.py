#!/usr/bin/python3
import sys
sys.path.append("../test")
from mininet.net import Mininet
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

        net = create_net(net_type)
        config_net_routing(net,net_type)

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
