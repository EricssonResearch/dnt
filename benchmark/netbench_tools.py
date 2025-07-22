import sys
import argparse
sys.path.append("../test")
from utils import *

client_cmd = {
    "ping" : "ping -c 10 -q 192.168.2.1",
    "iperf2" : {
        "udp_100M"  : "iperf -c 192.168.2.1 -u -b 100M -i 0 -e",
        "udp_1G"    : "iperf -c 192.168.2.1 -u -b 1G -i 0 -e",
        "udp_10G"   : "iperf -c 192.168.2.1 -u -b 10G -i 0 -e",
        "tcp"       : "iperf -c 192.168.2.1 -i 0"
    },
    "iperf3" : {
        "udp_100M"  : "iperf3 -c 192.168.2.1 -u -b 100M -i 0",
        "udp_1G"    : "iperf3 -c 192.168.2.1 -u -b 1G -i 0",
        "udp_10G"   : "iperf3 -c 192.168.2.1 -u -b 10G -i 0",
        "tcp"       : "iperf3 -c 192.168.2.1 -i 0"
    }
}

srv_cmd = {
    "iperf2": {
        "udp" : "iperf -s -u",
        "tcp" : "iperf -s"
    },
    "iperf3" : {
        "tcp+udp" : "iperf3 -s"
    }
}

def run_tests(net,v_iperf):
    no_error = True
    if  v_iperf == "iperf2" or v_iperf == "iperf3" :
        switch_netns("l")
        iperf_srv = {}
        for cmd in srv_cmd[v_iperf]:
            print("Starting server(s): " + srv_cmd[v_iperf][cmd])
            iperf_srv["cmd"] = exec_bg(srv_cmd[v_iperf][cmd], OUT_PIPE)
        time.sleep(1)

        switch_netns("t")

        for cmd in client_cmd[v_iperf]:
            print("Running: " + client_cmd[v_iperf][cmd])
            iperf_output = exec_fg( client_cmd[v_iperf][cmd] )
            if iperf_output.returncode != 0 :
                print("Error at running the benchmark tool:", iperf_output.stderr)
                no_error = False
            else:
                print(iperf_output.stdout)
    else:
        print("Unknown iperf version, use \"iperf2\" or \"iperf3\".")

    print("Running: " + client_cmd["ping"])
    ping_output = exec_fg(client_cmd["ping"])
    if ping_output.returncode != 0 :
        print("Error at running the benchmark tool:", ping_output.stderr)
        no_error = False
    else:
        print(ping_output.stdout)

    return no_error

list_ops=("fwd","encap-tsn","repl-tsn")

def parse_cmdline():
    parser = argparse.ArgumentParser()
    parser.add_argument("config",choices=list_ops, help="r2dtwo configuration")
    parser.add_argument("-d","--debug", help="debug mode", action="store_true")
    parser.add_argument("-i","--iperf", choices=("2","3"),help="iperf version to use, default is v2", default="2")
    parser.add_argument("-t","--topo", choices=("1hop","2hop"),help="topology to run on, default 1hop", default="1hop")

    args=parser.parse_args()

    return (args.config, args.topo, "iperf2" if args.iperf == "2" else "iperf3", args.debug)

