import sys
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


def parse_cmdline():
    # total arguments
    n = len(sys.argv)

    if n == 1 :
        print("Default topoplogy: 1hop, default iperf version: 2")
        return ("1hop","iperf2")
    elif n > 1 :
        topo = sys.argv[1]
        if topo == "1hop" or topo == "2hop":
            if n == 2 :
                print("Selected topology: ", topo, ", default iperf version: 2")
                return(topo,"iperf2")
        else:
            print("Unknown topology: ", topo, " Use 1hop or 2hop as first argument")
            print("Default topoplogy: 1hop, default iperf version: 2")
            return ("1hop","iperf2")
        if n > 2:
            iperf_ver = sys.argv[2]
        if iperf_ver == "2" or iperf_ver == "3":
            iperf_ver_string = "iperf2" if iperf_ver == "2" else "iperf3"
            print("Selected topology: ",topo, ", selected iperf version: ", iperf_ver_string)
            return(topo, iperf_ver_string)
        else:
            print("Unknown iperf version: : ", iperf_ver, " Use 2 or 3 as second argument")
            print("Topoplogy: ",topo, " default iperf version: 2")
            return (topo,"iperf2")
